#include "KaCrashGuard.h"
#include "core/KaLogExcept.h"
#include "KaStrata3dLink.h"
#include "KaTerrain3dStudio.h"
#include "KaUserError.h"

#include "KaTerrain3dView.h"
#include "core/LayerOps.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QCursor>
#include <QSet>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <qgscoordinatereferencesystem.h>
#include <qgspointxy.h>
#include <qgscoordinatetransform.h>
#include <qgscoordinatetransformcontext.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsmaprenderercustompainterjob.h>
#include <qgsmapsettings.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsrectangle.h>
#include <qgsvectorlayer.h>

namespace {

QgsRectangle demExtent(const Terrain3dService::DemScene& sc) {
  double xMin = 0, yMin = 0, xMax = 0, yMax = 0;
  if (!Terrain3dService::demWorldRect(sc, &xMin, &yMin, &xMax, &yMax))
    return QgsRectangle();
  return QgsRectangle(xMin, yMin, xMax, yMax);
}

QImage renderLayersToExtent(const QList<QgsMapLayer*>& layers, const QgsRectangle& ext,
                            const QgsCoordinateReferenceSystem& crs,
                            const QgsCoordinateTransformContext& xform,
                            const QSize& outSize = QSize(3072, 3072)) {
  if (layers.isEmpty() || ext.isEmpty())
    return {};
  QgsMapSettings s;
  s.setLayers(layers);
  s.setDestinationCrs(crs);
  s.setTransformContext(xform);
  s.setExtent(ext);
  s.setOutputSize(outSize);
  s.setOutputDpi(96);
  s.setBackgroundColor(Qt::transparent);
  QImage img(s.outputSize(), QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  QPainter p(&img);
  QgsMapRendererCustomPainterJob job(s, &p);
  job.start();
  job.waitForFinishedWithEventLoop();
  p.end();
  return img;
}

}  // namespace

KaTerrain3dStudio::KaTerrain3dStudio(QgsProject* project, QgsMapCanvas* canvas, QWidget* parent)
    : QWidget(parent), m_project(project), m_canvas(canvas) {
  setObjectName(QStringLiteral("terrain3dStudio"));
  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* panel = new QWidget(this);
  panel->setObjectName(QStringLiteral("terrain3dPanel"));
  panel->setMinimumWidth(200);
  panel->setMaximumWidth(260);
  auto* vl = new QVBoxLayout(panel);
  vl->setContentsMargins(10, 10, 10, 10);
  vl->setSpacing(8);
  auto* cap = new QLabel(QStringLiteral("입체지형"), panel);
  cap->setObjectName(QStringLiteral("cardCaption"));
  vl->addWidget(cap);
  m_emptyHint = new QLabel(
      QStringLiteral("지금 지도에 보이는 화면을 고해상 입체로 만듭니다. "
                     "탭을 열면 자동으로 시작합니다."),
      panel);
  m_emptyHint->setObjectName(QStringLiteral("emptyState"));
  m_emptyHint->setWordWrap(true);
  vl->addWidget(m_emptyHint);

  auto* btnFill = new QPushButton(QStringLiteral("화면을 입체로"), panel);
  btnFill->setObjectName(QStringLiteral("terrainAutoFillBtn"));
  auto* btnSave = new QPushButton(QStringLiteral("이미지 저장"), panel);
  btnSave->setObjectName(QStringLiteral("terrainExportBtn"));
  auto* btnSheet = new QPushButton(QStringLiteral("입체지형 도면출력"), panel);
  btnSheet->setObjectName(QStringLiteral("terrainSheetBtn"));
  vl->addWidget(btnFill);
  vl->addWidget(btnSave);
  vl->addWidget(btnSheet);
  auto* zLab = new QLabel(QStringLiteral("높이 과장"), panel);
  m_zExag = new QDoubleSpinBox(panel);
  m_zExag->setRange(0.2, 12.0);
  m_zExag->setSingleStep(0.1);
  m_zExag->setValue(2.0);
  m_zExag->setDecimals(1);
  m_zExag->setSuffix(QStringLiteral(" 배"));
  vl->addWidget(zLab);
  vl->addWidget(m_zExag);
  m_status = new QLabel(QStringLiteral("지도 화면을 입체로 만드는 중…"), panel);
  m_status->setWordWrap(true);
  m_status->setObjectName(QStringLiteral("terrain3dStatus"));
  vl->addWidget(m_status);
  vl->addStretch(1);
  new KaStrata3dLink(vl, m_status, m_project, this);

  m_view = new KaTerrain3dView(this);
  m_view->setObjectName(QStringLiteral("terrain3dView"));

  auto* center = new QWidget(this);
  auto* cl = new QHBoxLayout(center);
  cl->setContentsMargins(0, 0, 0, 0);
  cl->setSpacing(0);
  cl->addWidget(panel, 0);
  cl->addWidget(m_view, 1);

  root->addWidget(center, 1);

  connect(btnFill, &QPushButton::clicked, this, &KaTerrain3dStudio::tryAutoFill);
  connect(btnSave, &QPushButton::clicked, this, &KaTerrain3dStudio::saveImage);
  connect(btnSheet, &QPushButton::clicked, this, &KaTerrain3dStudio::runExportSheet);
  connect(m_zExag, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
    rebuildMesh();
  });
}

bool KaTerrain3dStudio::hasScene() const {
  return m_haveScene && m_view && m_view->hasMesh();
}

void KaTerrain3dStudio::rebuildMesh() {
  if (!m_haveScene || !m_view)
    return;
  const Terrain3dService::Mesh mesh =
      Terrain3dService::buildMesh(m_scene, 160, static_cast<float>(m_zExag->value()));
  if (m_texture.isNull())
    m_texture = Terrain3dService::hillshadeTexture(m_scene);
  m_view->setMesh(mesh, m_texture);
  if (m_emptyHint)
    m_emptyHint->setVisible(false);
  m_status->setText(QStringLiteral("DEM %1×%2 · 높이 %3–%4 m · 끌어서 돌리기")
                        .arg(m_scene.width)
                        .arg(m_scene.height)
                        .arg(m_scene.zMin, 0, 'f', 1)
                        .arg(m_scene.zMax, 0, 'f', 1));
}

void KaTerrain3dStudio::applyTexture(const QImage& tex, const QString& status) {
  if (tex.isNull())
    return;
  m_texture = tex;
  if (m_view)
    m_view->setTexture(m_texture);
  if (!status.isEmpty())
    m_status->setText(status);
}

QString KaTerrain3dStudio::crsLabel() const {
  if (!m_scene.projectionWkt.isEmpty()) {
    QgsCoordinateReferenceSystem crs;
    crs.createFromWkt(m_scene.projectionWkt);
    if (crs.isValid() && !crs.authid().isEmpty())
      return crs.authid();
  }
  if (m_project && m_project->crs().isValid() && !m_project->crs().authid().isEmpty())
    return m_project->crs().authid();
  return QStringLiteral("EPSG:5186");
}

QgsRasterLayer* KaTerrain3dStudio::ensureGoogleSatLayer() {
  LayerOps::ensureTileNetworkIdentity();
  if (m_googleSat && m_googleSat->isValid())
    return m_googleSat.data();
  delete m_googleSat;
  auto* rl = new QgsRasterLayer(Terrain3dService::googleSatelliteXyzUri(),
                                QStringLiteral("입체지형 위성"), QStringLiteral("wms"));
  if (!rl->isValid()) {
    delete rl;
    m_googleSat = nullptr;
    return nullptr;
  }
  rl->setParent(this);
  rl->setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:3857")));
  // 프로젝트·범례·캔버스에 넣지 않음. 탭이 소유하고 오프스크린만 쓴다.
  m_googleSat = rl;
  return rl;
}

QImage KaTerrain3dStudio::captureSatellite() {
  if (!m_haveScene || !m_project)
    return {};
  QgsRasterLayer* google = ensureGoogleSatLayer();
  QList<QgsMapLayer*> layers;
  QSet<QgsMapLayer*> seen;
  auto addTop = [&](QgsMapLayer* l) {
    if (!l || !l->isValid() || seen.contains(l))
      return;
    seen.insert(l);
    layers.append(l);
  };
  if (m_canvas) {
    const auto canvasLayers = m_canvas->layers();
    for (QgsMapLayer* l : canvasLayers)
      addTop(l);
  }
  const auto all = m_project->mapLayers();
  for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
    QgsMapLayer* l = it.value();
    if (!l)
      continue;
    const QString key = LayerOps::layerKeyOf(l);
    const QString n = l->name();
    const bool want = key == QLatin1String("survey_area") || n.contains(QString::fromUtf8("지질")) ||
                      n.contains(QString::fromUtf8("토양"));
    if (!want || !LayerOps::isLayerVisible(m_project, n))
      continue;
    addTop(l);
  }
  if (google)
    layers.append(google);
  if (QgsVectorLayer* sa = LayerOps::findByLayerKey(m_project, QStringLiteral("survey_area"))) {
    QgsMapLayer* top = sa;
    if (layers.contains(top)) {
      layers.removeAll(top);
      layers.prepend(top);
    }
  }
  QgsCoordinateReferenceSystem crs;
  if (!m_scene.projectionWkt.isEmpty())
    crs.createFromWkt(m_scene.projectionWkt);
  if (!crs.isValid())
    crs = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5186"));
  if (!crs.isValid() && m_project)
    crs = m_project->crs();
  if (layers.isEmpty() || !crs.isValid())
    return {};
  QImage best;
  for (int i = 0; i < 5; ++i) {
    const QImage img = renderLayersToExtent(layers, demExtent(m_scene), crs,
                                            m_project->transformContext(), QSize(3072, 3072));
    if (Terrain3dService::textureLooksFilled(img))
      return img;
    best = img;
    QApplication::processEvents();
  }
  return best;
}

void KaTerrain3dStudio::finishLoadedScene(const Terrain3dService::DemScene& sc, const QString& path) {
  m_scene = sc;
  m_haveScene = true;
  m_demPath = path;
  if (!path.startsWith(QLatin1String("/vsicurl")) && !path.contains(QLatin1String("http")))
    m_fileDir = QFileInfo(path).absolutePath();
  m_texture = Terrain3dService::hillshadeTexture(sc);
  m_textureKind.clear();
  rebuildMesh();
  m_status->setText(QStringLiteral("Google 고해상 위성을 입히는 중…"));
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QApplication::processEvents();
  const QImage sat = captureSatellite();
  QApplication::restoreOverrideCursor();
  if (Terrain3dService::textureLooksFilled(sat)) {
    m_textureKind = QStringLiteral("google");
    applyTexture(sat, QStringLiteral("지금 화면을 고해상 입체로 만들었습니다."));
  } else {
    m_status->setText(QStringLiteral("높이는 나왔습니다. 위성이 안 오면 「화면을 입체로」를 다시 누르세요."));
  }
}

bool KaTerrain3dStudio::loadDemClipToCanvas(const QString& path, QString* errorOut) {
  if (!m_canvas || m_canvas->extent().isEmpty() || !m_canvas->extent().isFinite()) {
    if (errorOut) *errorOut = QStringLiteral("지도 화면 범위가 없습니다.");
    return false;
  }
  QgsCoordinateReferenceSystem dest;
  QgsRasterLayer peek(path, QStringLiteral("peek"), QStringLiteral("gdal"));
  if (peek.isValid() && peek.crs().isValid())
    dest = peek.crs();
  if (!dest.isValid() && !m_scene.projectionWkt.isEmpty())
    dest.createFromWkt(m_scene.projectionWkt);
  if (!dest.isValid())
    dest = m_canvas->mapSettings().destinationCrs();
  QgsRectangle clip = m_canvas->extent();
  const QgsCoordinateReferenceSystem srcCrs = m_canvas->mapSettings().destinationCrs();
  if (srcCrs.isValid() && dest.isValid() && srcCrs != dest) {
    try {
      const QgsCoordinateTransform tr(srcCrs, dest, m_project ? m_project->transformContext()
                                                              : QgsCoordinateTransformContext());
      clip = tr.transformBoundingBox(clip);
    } catch (...) {
      KA_LOG_EXCEPT();
      if (errorOut) *errorOut = QStringLiteral("화면 범위를 DEM 좌표로 바꾸지 못했습니다.");
      return false;
    }
  }
  if (clip.isEmpty() || !clip.isFinite()) {
    if (errorOut) *errorOut = QStringLiteral("화면 범위를 자르지 못했습니다.");
    return false;
  }
  clip.scale(1.08);
  Terrain3dService::DemScene sc;
  QString err;
  if (!Terrain3dService::loadDemClip(path, clip.xMinimum(), clip.yMinimum(), clip.xMaximum(),
                                     clip.yMaximum(), 1024, &sc, &err)) {
    if (errorOut) *errorOut = err;
    return false;
  }
  finishLoadedScene(sc, path);
  return true;
}

bool KaTerrain3dStudio::loadDemPath(const QString& path, QString* errorOut) {
  if (m_canvas && !m_canvas->extent().isEmpty() && m_canvas->extent().isFinite())
    return loadDemClipToCanvas(path, errorOut);
  Terrain3dService::DemScene sc;
  QString err;
  if (!Terrain3dService::loadDem(path, &sc, &err)) {
    if (errorOut) *errorOut = err;
    return false;
  }
  finishLoadedScene(sc, path);
  return true;
}

QVector<Terrain3dService::DemCandidate> KaTerrain3dStudio::mapDemCandidates() const {
  QVector<Terrain3dService::DemCandidate> cands;
  if (!m_project)
    return cands;
  for (QgsMapLayer* l : m_project->mapLayers()) {
    auto* ras = qobject_cast<QgsRasterLayer*>(l);
    if (!ras || !ras->isValid())
      continue;
    if (ras->providerType() != QLatin1String("gdal") ||
        ras->customProperty(QStringLiteral("ka_hgis/section_raster")).toBool())
      continue;  // a section photo left by the removed 단면도 tab is never a DEM
    Terrain3dService::DemCandidate c;
    c.name = ras->name();
    c.source = ras->source();
    cands.append(c);
  }
  return cands;
}

void KaTerrain3dStudio::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  QTimer::singleShot(0, this, &KaTerrain3dStudio::tryAutoFill);
}

void KaTerrain3dStudio::tryAutoFill() {
  if (m_autoBusy)
    return;
  if (!m_canvas || m_canvas->extent().isEmpty() || !m_canvas->extent().isFinite()) {
    m_status->setText(QStringLiteral("지도 화면이 없습니다. 지도에서 위치를 맞춘 뒤 다시 여세요."));
    return;
  }
  const QgsRectangle e = m_canvas->extent();
  const QString key = QStringLiteral("%1,%2,%3,%4")
                          .arg(e.xMinimum(), 0, 'f', 1)
                          .arg(e.yMinimum(), 0, 'f', 1)
                          .arg(e.xMaximum(), 0, 'f', 1)
                          .arg(e.yMaximum(), 0, 'f', 1);
  if (m_haveScene && key == m_lastFillKey) {
    refreshDrape();
    return;
  }
  m_autoBusy = true;
  m_status->setText(QStringLiteral("지금 지도 화면을 고해상 입체로 만드는 중…"));
  QApplication::processEvents();
  QString err;
  bool ok = false;
  const QString picked = Terrain3dService::pickTerrainDemSource(mapDemCandidates());
  if (!picked.isEmpty()) {
    if (picked.contains(QLatin1String("vsicurl")) || picked.contains(QLatin1String("http")))
      ok = loadDemClipToCanvas(picked, &err);
    else
      ok = loadDemPath(picked, &err);
  }
  if (!ok) {
    QgsCoordinateReferenceSystem wgs(QStringLiteral("EPSG:4326"));
    QgsRectangle wgsExt = e;
    const QgsCoordinateReferenceSystem src = m_canvas->mapSettings().destinationCrs();
    if (src.isValid() && src != wgs) {
      try {
        const QgsCoordinateTransform tr(src, wgs, m_project ? m_project->transformContext()
                                                            : QgsCoordinateTransformContext());
        wgsExt = tr.transformBoundingBox(e);
      } catch (...) {
        KA_LOG_EXCEPT();
        wgsExt = QgsRectangle();
      }
    }
    if (!wgsExt.isEmpty() && wgsExt.isFinite()) {
      const QgsPointXY c = wgsExt.center();
      const QString uri = LayerOps::copernicusCogUriForWgs84(c.y(), c.x());
      ok = loadDemClipToCanvas(uri, &err);
    }
  }
  m_autoBusy = false;
  if (ok)
    m_lastFillKey = key;
  else if (m_status)
    m_status->setText(err.isEmpty() ? QStringLiteral("화면 높이를 받지 못했습니다. 네트워크를 확인하세요.")
                                    : err);
}

bool KaTerrain3dStudio::exportImage(const QString& path, QString* errorOut) {
  if (!m_view || !m_view->hasMesh()) {
    if (errorOut) *errorOut = QStringLiteral("먼저 지도 화면을 입체로 만드세요.");
    return false;
  }
  const QImage view = m_view->renderView(1280, 800);
  const double vis =
      Terrain3dService::visibleWidthAtTarget(m_view->distance(), view.width(), view.height());
  const double north =
      m_view->yawDeg() + Terrain3dService::northAzimuthDeg(m_scene.geotransform);
  const QImage out = Terrain3dService::composeExport(view, vis, north);
  if (!out.save(path)) {
    if (errorOut) *errorOut = QStringLiteral("이미지를 쓰지 못했습니다.");
    return false;
  }
  return true;
}

bool KaTerrain3dStudio::exportSheet(const QString& path, QString* errorOut) {
  if (!m_view || !m_view->hasMesh()) {
    if (errorOut) *errorOut = QStringLiteral("먼저 지도 화면을 입체로 만드세요.");
    return false;
  }
  const QImage view = m_view->renderView(1400, 860);
  const double vis =
      Terrain3dService::visibleWidthAtTarget(m_view->distance(), view.width(), view.height());
  const double north =
      m_view->yawDeg() + Terrain3dService::northAzimuthDeg(m_scene.geotransform);
  const QImage sheet = Terrain3dService::composeSheet(
      view, vis, north, crsLabel(), QFileInfo(m_demPath).fileName(), m_scene.zMin, m_scene.zMax);
  if (!sheet.save(path)) {
    if (errorOut) *errorOut = QStringLiteral("도면을 쓰지 못했습니다.");
    return false;
  }
  return true;
}

void KaTerrain3dStudio::saveImage() {
  if (!hasScene()) {
    QMessageBox::information(this, QStringLiteral("입체지형"),
                             QStringLiteral("먼저 지도 화면을 입체로 만드세요."));
    return;
  }
  const QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("이미지 저장"), QStringLiteral("입체지형.png"),
      QStringLiteral("PNG (*.png);;JPEG (*.jpg)"));
  if (path.isEmpty())
    return;
  QString err;
  if (!exportImage(path, &err)) {
    KaUserError::warn(this, {QStringLiteral("입체지형"), QStringLiteral("입체지형 그림을 저장하지 못했습니다"), err, {}});
    return;
  }
  m_status->setText(QStringLiteral("저장함 · 축척자·방위 포함\n%1").arg(QFileInfo(path).fileName()));
}

QImage KaTerrain3dStudio::renderView(int pixelW, int pixelH) const {
  if (!m_view)
    return {};
  return m_view->renderView(pixelW, pixelH);
}

bool KaTerrain3dStudio::groundExtent(double* xMin, double* yMin, double* xMax, double* yMax) const {
  if (!m_haveScene)
    return false;
  return Terrain3dService::demWorldRect(m_scene, xMin, yMin, xMax, yMax);
}

QString KaTerrain3dStudio::workCrsLabel() const {
  return crsLabel();
}

double KaTerrain3dStudio::visibleWidthM(int pixelW, int pixelH) const {
  if (!m_view)
    return 0;
  return Terrain3dService::visibleWidthAtTarget(m_view->distance(), pixelW, pixelH);
}

double KaTerrain3dStudio::northYawDeg() const {
  if (!m_view)
    return 0;
  return static_cast<double>(m_view->yawDeg()) + Terrain3dService::northAzimuthDeg(m_scene.geotransform);
}

float KaTerrain3dStudio::zMin() const {
  return m_scene.zMin;
}

float KaTerrain3dStudio::zMax() const {
  return m_scene.zMax;
}

QString KaTerrain3dStudio::demDisplayName() const {
  if (m_demPath.contains(QLatin1String("vsicurl")) || m_demPath.contains(QLatin1String("http")))
    return QStringLiteral("현재 화면");
  const QString n = QFileInfo(m_demPath).fileName();
  return n.isEmpty() ? QStringLiteral("현재 화면") : n;
}

void KaTerrain3dStudio::setVisibleWidthM(double widthM, int pixelW, int pixelH) {
  if (!m_view || !(widthM > 0.0))
    return;
  const double d = Terrain3dService::distanceForVisibleWidth(widthM, pixelW, pixelH);
  m_view->setDistance(static_cast<float>(d));
}

void KaTerrain3dStudio::refreshDrape() {
  if (!m_haveScene)
    return;
  const QImage sat = captureSatellite();
  if (!sat.isNull())
    applyTexture(sat, QString());
}

void KaTerrain3dStudio::runExportSheet() {
  if (!hasScene()) {
    QMessageBox::information(this, QStringLiteral("입체지형 도면출력"),
                             QStringLiteral("먼저 지도 화면을 입체로 만드세요."));
    return;
  }
  emit requestDrawingStudio();
}
