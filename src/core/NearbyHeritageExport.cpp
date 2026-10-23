#include "NearbyHeritageExport.h"

#include <cpl_error.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>

namespace NearbyHeritageExport {

bool writeXlsx(const QString& path, const QVector<Item>& items, QString* errorOut) {
  QTemporaryDir tempDir;
  if (path.trimmed().isEmpty() || !tempDir.isValid()) {
    if (errorOut) *errorOut = QStringLiteral("엑셀 저장 경로 또는 임시 폴더가 유효하지 않습니다.");
    return false;
  }
  const QString scratchXlsx = tempDir.filePath(QStringLiteral("nearby_heritage.xlsx"));
  GDALAllRegister();
  CPLErrorReset();
  GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("XLSX");
  GDALDataset* ds = driver ? driver->Create(scratchXlsx.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr) : nullptr;
  OGRLayer* sheet = ds ? ds->CreateLayer("주변유적_현황", nullptr, wkbNone, nullptr) : nullptr;
  auto fail = [&](const QString& msg) {
    if (ds) GDALClose(ds);
    if (errorOut) *errorOut = msg;
    return false;
  };
  if (!sheet) return fail(QStringLiteral("엑셀 시트를 만들지 못했습니다."));
  const QPair<const char*, OGRFieldType> kCols[] = {
      {"연번", OFTInteger},        {"자료구분", OFTString},   {"레이어명", OFTString},
      {"유적명", OFTString},       {"유산코드", OFTString},   {"보고서명(서지)", OFTString},
      {"사업명", OFTString},       {"소재지", OFTString},     {"조사기관", OFTString},
      {"시대", OFTString},         {"유적유무", OFTString},   {"허가번호", OFTString},
      {"반경구분", OFTString},     {"방향", OFTString},       {"거리(m)", OFTInteger},
      {"방위각(도)", OFTString},   {"위치설명", OFTString}};
  for (const auto& col : kCols) {
    OGRFieldDefn fd(col.first, col.second);
    if (sheet->CreateField(&fd) != OGRERR_NONE)
      return fail(QStringLiteral("엑셀 열(%1) 생성에 실패했습니다.").arg(QString::fromUtf8(col.first)));
  }
  for (int i = 0; i < items.size(); ++i) {
    const Item& it = items.at(i);
    OGRFeature* feat = OGRFeature::CreateFeature(sheet->GetLayerDefn());
    const QString az = (it.azimuthDeg < 0.0) ? QStringLiteral("-") : QString::number(it.azimuthDeg, 'f', 1);
    feat->SetField(0, i + 1);
    const QString texts[] = {it.datasetName,   it.layerName,    it.siteName,   it.heritageCode,
                             it.reportName,    it.projectName,  it.siteAddress, it.institution,
                             it.era,           it.heritagePresence, it.permitNumber};
    for (int c = 0; c < 11; ++c) feat->SetField(c + 1, texts[c].toUtf8().constData());
    feat->SetField(12, it.radiusLabel.toUtf8().constData());
    feat->SetField(13, it.direction.toUtf8().constData());
    feat->SetField(14, it.distanceMeters);
    feat->SetField(15, az.toUtf8().constData());
    feat->SetField(16, it.description.toUtf8().constData());
    const OGRErr err = sheet->CreateFeature(feat);
    OGRFeature::DestroyFeature(feat);
    if (err != OGRERR_NONE) return fail(QStringLiteral("엑셀 행 기록에 실패했습니다."));
  }
  GDALClose(ds);
  ds = nullptr;
  QFile src(scratchXlsx);
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile dst(path);
  if (!src.open(QIODevice::ReadOnly) || !dst.open(QIODevice::WriteOnly))
    return fail(QStringLiteral("엑셀 저장 경로를 열지 못했습니다: %1").arg(path));
  const QByteArray bytes = src.readAll();
  if (dst.write(bytes) != bytes.size() || !dst.commit())
    return fail(QStringLiteral("엑셀 파일을 저장하지 못했습니다: %1").arg(path));
  return true;
}

Result exportToXlsx(QgsProject* project, const QString& path, QgsVectorLayer* sourceLayer,
                    double maxRadiusMeters) {
  Result res = collect(project, sourceLayer, maxRadiusMeters);
  if (res.ok()) writeXlsx(path, res.items, &res.error);
  return res;
}
}  // namespace NearbyHeritageExport
