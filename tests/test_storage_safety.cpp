#include <stdexcept>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "core/KaSafeQgis.h"
#include "core/LayerOps.h"
#include "core/SurveyProjectFactory.h"
#include "core/SurveySession.h"
#include "core/SurveyStorage.h"
#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsfeature.h>
#include <qgsgeometry.h>
#include <qgsrectangle.h>
#include <qgspointxy.h>
#include <qgslayertree.h>
#include <qgsrasterlayer.h>
#include <qgsvectorfilewriter.h>
#include <qgsvectorlayer.h>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

class ReadLock {
 public:
  explicit ReadLock(const QString& path)
      : m_handle(CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
  ~ReadLock() {
    if (valid()) CloseHandle(m_handle);
  }
  bool valid() const { return m_handle != INVALID_HANDLE_VALUE; }
  ReadLock(const ReadLock&) = delete;
  ReadLock& operator=(const ReadLock&) = delete;

 private:
  HANDLE m_handle;
};
#endif

static QByteArray fileHash(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file)) return {};
  return hash.result();
}

static bool writeBytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

static QStringList temporaryArtifacts(const QString& path) {
  return QDir(path).entryList({QStringLiteral(".ka-new-survey-*"), QStringLiteral("*ka-writing*")},
                              QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
}

static QString writeHeritageGpkg(const QString& dest, QgsProject* project, QString* errorOut) {
  QgsVectorLayer memory(QStringLiteral("Polygon?crs=EPSG:5186&field=name:string"),
                        QStringLiteral("heritage"), QStringLiteral("memory"));
  if (!memory.isValid()) {
    if (errorOut) *errorOut = QStringLiteral("memory layer");
    return {};
  }
  QgsFeature feature(memory.fields());
  feature.setAttribute(0, QStringLiteral("국가지정유산"));
  feature.setGeometry(QgsGeometry::fromRect(QgsRectangle(200000, 450000, 200100, 450100)));
  if (!memory.dataProvider()->addFeature(feature)) {
    if (errorOut) *errorOut = QStringLiteral("add feature");
    return {};
  }
  QgsVectorFileWriter::SaveVectorOptions options;
  options.driverName = QStringLiteral("GPKG");
  options.layerName = QStringLiteral("heritage");
  options.fileEncoding = QStringLiteral("UTF-8");
  QString writeError;
  if (QgsVectorFileWriter::writeAsVectorFormatV3(&memory, dest, project->transformContext(), options,
                                                 &writeError) != QgsVectorFileWriter::NoError) {
    if (errorOut) *errorOut = writeError;
    return {};
  }
  return dest + QStringLiteral("|layername=heritage");
}

class TestStorageSafety : public QObject {
  Q_OBJECT
 private slots:
  void recoverySnapshot_preservesPendingEditsAndSource_data() {
    QTest::addColumn<QString>("crs");
    QTest::newRow("central") << QStringLiteral("EPSG:5186");
    QTest::newRow("east") << QStringLiteral("EPSG:5187");
  }

  void recoverySnapshot_preservesPendingEditsAndSource() {
    QFETCH(QString, crs);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString source = SurveyProjectFactory::createNewSurvey(
        dir.path(), QStringLiteral("원본조사"), &error, crs);
    QVERIFY2(!source.isEmpty(), qPrintable(error));
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(crs));
    project.setTitle(QStringLiteral("복구 전 현재 작업"));
    auto* area = LayerOps::ensureDomainLayer(&project, source, QStringLiteral("survey_area"),
                                             QStringLiteral("조사구역"), &error);
    auto* line = LayerOps::ensureDomainLayer(&project, source, QStringLiteral("feature_line"),
                                             QStringLiteral("미저장 선"), &error);
    QVERIFY2(area && line, qPrintable(error));
    for (auto* layer : {area, line}) {
      QVERIFY(layer->startEditing());
      QgsFeature feature(layer->fields());
      feature.setAttribute(QStringLiteral("note"), QStringLiteral("기존"));
      feature.setGeometry(layer == area
          ? QgsGeometry::fromRect(QgsRectangle(190000, 560000, 190010, 560010))
          : QgsGeometry::fromWkt(QStringLiteral("LineString (190000 560000, 190010 560010)")));
      QVERIFY(layer->addFeature(feature));
      QVERIFY(layer->commitChanges(false));
    }
    const QgsFeatureId areaId = *area->allFeatureIds().constBegin();
    QVERIFY(area->changeAttributeValue(areaId, area->fields().indexOf(QStringLiteral("note")),
                                       QStringLiteral("수정된 면")));
    QgsGeometry changedArea = QgsGeometry::fromRect(QgsRectangle(190020, 560020, 190040, 560040));
    QVERIFY(area->changeGeometry(areaId, changedArea));
    QgsFeature added(area->fields());
    added.setAttribute(QStringLiteral("note"), QStringLiteral("새로 추가한 면"));
    added.setGeometry(QgsGeometry::fromRect(QgsRectangle(190050, 560050, 190060, 560060)));
    QVERIFY(area->addFeature(added)); // NULL fid + existing fid must both survive writer.
    const QgsFeatureId lineId = *line->allFeatureIds().constBegin();
    QVERIFY(line->deleteFeature(lineId));
    QgsFeature replacement(line->fields());
    replacement.setAttribute(QStringLiteral("note"), QStringLiteral("삭제 뒤 대체한 선"));
    replacement.setGeometry(QgsGeometry::fromWkt(QStringLiteral("LineString (190030 560030, 190080 560080)")));
    QVERIFY(line->addFeature(replacement));
    auto* reference = new QgsVectorLayer(
        QStringLiteral("Point?crs=%1&field=note:string").arg(crs), QStringLiteral("참조점"), QStringLiteral("memory"));
    QVERIFY(reference->isValid());
    LayerOps::markReferenceLayer(reference);
    project.addMapLayer(reference);
    QVERIFY(reference->startEditing());
    QgsFeature point(reference->fields());
    point.setAttribute(QStringLiteral("note"), QStringLiteral("메모리에만 있는 점"));
    point.setGeometry(QgsGeometry::fromWkt(QStringLiteral("Point (190070 560070)")));
    QVERIFY(reference->addFeature(point));
    project.layerTreeRoot()->findLayer(reference->id())->setItemVisibilityChecked(false);
    auto* missingRaster = new QgsRasterLayer(dir.filePath(QStringLiteral("없는사진.tif")),
                                             QStringLiteral("원본이 없는 사진"), QStringLiteral("gdal"));
    QVERIFY(!missingRaster->isValid());
    LayerOps::markReferenceLayer(missingRaster);
    project.addMapLayer(missingRaster);
    project.setFileName(dir.filePath(QStringLiteral("현재작업.qgz")));
    project.setPresetHomePath(dir.path());
    project.setDirty(true);
    area->setAllowCommit(false);
    QVERIFY(!area->commitChanges(false));
    const QByteArray before = fileHash(source);
    const QString originalFile = project.fileName();
    const QString originalHome = project.presetHomePath();
    QMap<QString, QString> sources;
    QMap<QString, QMap<QString, QgsFeature>> expected;
    for (auto* layer : {area, line, reference}) {
      sources.insert(layer->name(), layer->source());
      auto iterator = layer->getFeatures();
      QgsFeature feature;
      while (iterator.nextFeature(feature))
        expected[layer->name()].insert(feature.attribute(QStringLiteral("note")).toString(), feature);
    }
    const QString folder = dir.filePath(QStringLiteral("복구 사본"));
    const QString first = SurveyStorage::writeRecoverySnapshot(&project, folder, &error);
    QVERIFY2(!first.isEmpty(), qPrintable(error));
    QVERIFY(QFileInfo::exists(first));
    QCOMPARE(fileHash(source), before);
    QCOMPARE(project.fileName(), originalFile);
    QCOMPARE(project.presetHomePath(), originalHome);
    QVERIFY(project.isDirty());
    for (auto* layer : {area, line, reference}) {
      QCOMPARE(layer->source(), sources.value(layer->name()));
      QVERIFY(layer->isEditable() && layer->isModified());
    }
    QgsProject restored;
    QVERIFY2(restored.read(SurveyStorage::projectUri(first)), qPrintable(restored.error()));
    QCOMPARE(restored.crs().authid(), crs);
    QCOMPARE(restored.mapLayers().size(), 4);
    const auto missing = restored.mapLayersByName(QStringLiteral("원본이 없는 사진"));
    QCOMPARE(missing.size(), 1);
    QVERIFY(!missing.first()->isValid());
    const QStringList external = restored.readListEntry(QStringLiteral("ka_hgis"),
                                                        QStringLiteral("recovery_external_references"));
    QVERIFY(external.join(QLatin1Char('\n')).contains(QStringLiteral("원본이 없는 사진")));
    for (auto* original : {area, line, reference}) {
      const auto matches = restored.mapLayersByName(original->name());
      QCOMPARE(matches.size(), 1);
      auto* snapshot = qobject_cast<QgsVectorLayer*>(matches.first());
      QVERIFY(snapshot && snapshot->isValid());
      QCOMPARE(snapshot->crs().authid(), original->crs().authid());
      QCOMPARE(LayerOps::layerKeyOf(snapshot), LayerOps::layerKeyOf(original));
      QCOMPARE(LayerOps::isReferenceLayer(snapshot), LayerOps::isReferenceLayer(original));
      QCOMPARE(snapshot->featureCount(), qint64(expected.value(original->name()).size()));
      auto* node = restored.layerTreeRoot()->findLayer(snapshot->id());
      QVERIFY(node);
      QCOMPARE(node->itemVisibilityChecked(),
               project.layerTreeRoot()->findLayer(original->id())->itemVisibilityChecked());
      auto iterator = snapshot->getFeatures();
      QgsFeature feature;
      while (iterator.nextFeature(feature)) {
        const QString key = feature.attribute(QStringLiteral("note")).toString();
        QVERIFY(expected.value(original->name()).contains(key));
        const QgsFeature prior = expected.value(original->name()).value(key);
        QVERIFY(feature.geometry().isExactlyEqual(prior.geometry()));
        for (const QgsField& field : original->fields()) {
          QVERIFY(snapshot->fields().lookupField(field.name()) >= 0);
          // 저장 왕복에서 생기는 차이는 데이터 손실이 아니다.
          // - 아직 저장되지 않은 도형의 fid 는 비어 있고, GPKG 가 기록할 때 새로 매긴다.
          // - 설정하지 않은 값은 NULL 로 두어도 빈 값으로 돌아온다.
          // 값이 있던 항목은 그대로 유지되어야 하며, 그것만 엄격히 비교한다.
          const QVariant actual = feature.attribute(field.name());
          const QVariant expectedValue = prior.attribute(field.name());
          if (expectedValue.isNull()) {
            const bool isFid = field.name().compare(QLatin1String("fid"), Qt::CaseInsensitive) == 0;
            if (isFid || actual.isNull() || actual.toString().isEmpty()) continue;
          }
          QCOMPARE(actual, expectedValue);
        }
      }
    }
    restored.clear();
    const QByteArray firstHash = fileHash(first);
    const QString second = SurveyStorage::writeRecoverySnapshot(&project, folder, &error);
    QVERIFY2(!second.isEmpty(), qPrintable(error));
    QVERIFY(second != first);
    QCOMPARE(fileHash(first), firstHash);
    QCOMPARE(fileHash(source), before);
    const QString blocked = dir.filePath(QStringLiteral("파일이라 폴더가 아님"));
    QVERIFY(writeBytes(blocked, QByteArray("keep")));
    QVERIFY(SurveyStorage::writeRecoverySnapshot(&project, blocked, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QCOMPARE(fileHash(first), firstHash);
    QCOMPARE(fileHash(source), before);
    QVERIFY(project.isDirty());
    for (auto* layer : {area, line, reference}) QVERIFY(layer->isEditable() && layer->isModified());
    area->setAllowCommit(true);
    for (auto* layer : {area, line, reference}) layer->rollBack();
  }

  void newSurvey_existingFilesRemainByteIdentical() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString gpkg = SurveyProjectFactory::createNewSurvey(dir.path(), QStringLiteral("보존할조사"), &error,
                                                               QStringLiteral("EPSG:5187"));
    QVERIFY2(!gpkg.isEmpty(), qPrintable(error));
    const QString qgz = dir.filePath(QStringLiteral("보존할조사.qgz"));
    const auto gpkgBefore = fileHash(gpkg);
    const auto qgzBefore = fileHash(qgz);
    QVERIFY(!gpkgBefore.isEmpty() && !qgzBefore.isEmpty());
    const QString duplicate = SurveyProjectFactory::createNewSurvey(
        dir.path(), QStringLiteral("보존할조사"), &error, QStringLiteral("EPSG:5186"));
    QCOMPARE(fileHash(gpkg), gpkgBefore);
    QCOMPARE(fileHash(qgz), qgzBefore);
    QVERIFY(duplicate.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(temporaryArtifacts(dir.path()).isEmpty());
  }

  void newSurvey_orphanWorkspaceIsNotOverwritten() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString qgz = dir.filePath(QStringLiteral("보존할작업.qgz"));
    QVERIFY(writeBytes(qgz, QByteArray("existing workspace bytes")));
    const auto before = fileHash(qgz);
    QString error;
    const QString duplicate = SurveyProjectFactory::createNewSurvey(
        dir.path(), QStringLiteral("보존할작업"), &error, QStringLiteral("EPSG:5187"));
    QCOMPARE(fileHash(qgz), before);
    QVERIFY(duplicate.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("보존할작업.gpkg"))));
    QVERIFY(temporaryArtifacts(dir.path()).isEmpty());
  }

  void newSurvey_invalidCrsLeavesNoPartialFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    QVERIFY(SurveyProjectFactory::createNewSurvey(dir.path(), QStringLiteral("실패한조사"), &error,
                                                  QStringLiteral("EPSG:999999999"))
                .isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(QDir(dir.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
  }

  void newSurvey_workspaceCollisionLeavesNoPartialGpkg() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString qgz = dir.filePath(QStringLiteral("막힌조사.qgz"));
    QVERIFY(QDir().mkpath(qgz));
    QVERIFY(writeBytes(QDir(qgz).filePath(QStringLiteral("keep.txt")), QByteArray("keep")));
    QString error;
    QVERIFY(SurveyProjectFactory::createNewSurvey(dir.path(), QStringLiteral("막힌조사"), &error,
                                                  QStringLiteral("EPSG:5187"))
                .isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(QFileInfo::exists(QDir(qgz).filePath(QStringLiteral("keep.txt"))));
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("막힌조사.gpkg"))));
    QVERIFY(temporaryArtifacts(dir.path()).isEmpty());
  }

  void newSurvey_schemaExistsButWorkspaceIsEmpty_data() {
    QTest::addColumn<QString>("crs");
    QTest::newRow("central") << QStringLiteral("EPSG:5186");
    QTest::newRow("east") << QStringLiteral("EPSG:5187");
  }

  void newSurvey_schemaExistsButWorkspaceIsEmpty() {
    QFETCH(QString, crs);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString gpkg = SurveyProjectFactory::createNewSurvey(dir.path(), QStringLiteral("빈조사"), &error, crs);
    QVERIFY2(!gpkg.isEmpty(), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY2(SurveyStorage::validateForOpen(gpkg, &error), qPrintable(error));
    QgsVectorLayer survey(gpkg + QStringLiteral("|layername=survey_area"), QStringLiteral("schema"),
                          QStringLiteral("ogr"));
    QVERIFY(survey.isValid());
    QCOMPARE(survey.featureCount(), 0LL);
    QCOMPARE(survey.crs().authid(), crs);
    QgsProject workspace;
    QVERIFY(workspace.read(dir.filePath(QStringLiteral("빈조사.qgz"))));
    QCOMPARE(workspace.crs().authid(), crs);
    QCOMPARE(workspace.mapLayers().size(), 0);
    QVERIFY(temporaryArtifacts(dir.path()).isEmpty());
  }

  void atomicWorkspace_preservesRelativeSourceAndPreviousGeneration() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString gpkg =
        SurveyProjectFactory::createNewSurvey(dir.path(), QStringLiteral("왕복"), &error, QStringLiteral("EPSG:5187"));
    QVERIFY2(!gpkg.isEmpty(), qPrintable(error));
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5187")));
    auto* vector = new QgsVectorLayer(gpkg + QStringLiteral("|layername=survey_area"), QStringLiteral("구역"),
                                      QStringLiteral("ogr"));
    QVERIFY(vector->isValid());
    project.addMapLayer(vector);
    const QString path = dir.filePath(QStringLiteral("작업 화면.qgz"));
    project.setTitle(QStringLiteral("첫 저장"));
    QVERIFY2(kaWriteQgisProjectAtomic(&project, path, &error), qPrintable(error));
    const auto before = fileHash(path);
    project.setTitle(QStringLiteral("두 번째 저장"));
    QVERIFY2(kaWriteQgisProjectAtomic(&project, path, &error), qPrintable(error));
    QCOMPARE(fileHash(kaProjectBackupPath(path)), before);
    QCOMPARE(project.fileName(), QFileInfo(path).absoluteFilePath());
    QgsProject reopened;
    QVERIFY(reopened.read(path));
    QCOMPARE(reopened.title(), QStringLiteral("두 번째 저장"));
    QCOMPARE(reopened.mapLayers().size(), 1);
    QVERIFY(reopened.mapLayers().first()->isValid());
    QCOMPARE(QFileInfo(reopened.mapLayers().first()->source().section(QLatin1Char('|'), 0, 0)).canonicalFilePath(),
             QFileInfo(gpkg).canonicalFilePath());
    QVERIFY(temporaryArtifacts(dir.path()).isEmpty());
  }

  void atomicWorkspace_lockedTargetPreservesFileAndUnsavedState() {
#ifdef Q_OS_WIN
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("잠긴작업.qgz"));
    QgsProject project;
    project.setTitle(QStringLiteral("이전 내용"));
    QString error;
    QVERIFY2(kaWriteQgisProjectAtomic(&project, path, &error), qPrintable(error));
    const auto before = fileHash(path);
    project.setFileName(QStringLiteral("original-session.qgz"));
    project.setTitle(QStringLiteral("아직 저장하지 못한 내용"));
    project.setDirty(true);
    ReadLock lock(path);
    QVERIFY(lock.valid());
    QVERIFY(!kaWriteQgisProjectAtomic(&project, path, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(fileHash(path), before);
    QCOMPARE(project.fileName(), QStringLiteral("original-session.qgz"));
    QVERIFY(project.isDirty());
    QVERIFY(temporaryArtifacts(dir.path()).isEmpty());
#else
    QSKIP("Windows sharing-lock failure contract");
#endif
  }

  void atomicWorkspace_lockedBackupPreservesBothFiles() {
#ifdef Q_OS_WIN
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("백업실패.qgz"));
    const QString backup = kaProjectBackupPath(path);
    QgsProject project;
    QString error;
    project.setTitle(QStringLiteral("첫 저장"));
    QVERIFY2(kaWriteQgisProjectAtomic(&project, path, &error), qPrintable(error));
    project.setTitle(QStringLiteral("두 번째 저장"));
    QVERIFY2(kaWriteQgisProjectAtomic(&project, path, &error), qPrintable(error));
    const auto before = fileHash(path), backupBefore = fileHash(backup);
    project.setTitle(QStringLiteral("저장되지 않을 세 번째"));
    project.setDirty(true);
    ReadLock lock(backup);
    QVERIFY(lock.valid());
    QVERIFY(!kaWriteQgisProjectAtomic(&project, path, &error));
    QCOMPARE(fileHash(path), before);
    QCOMPARE(fileHash(backup), backupBefore);
    QVERIFY(project.isDirty());
    QVERIFY(temporaryArtifacts(dir.path()).isEmpty());
#else
    QSKIP("Windows sharing-lock failure contract");
#endif
  }

  void atomicWorkspace_serializationExceptionKeepsFileAndState() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("예외복구.qgz"));
    QgsProject project;
    QString error;
    QVERIFY2(kaWriteQgisProjectAtomic(&project, path, &error), qPrintable(error));
    const auto before = fileHash(path);
    project.setFileName(QStringLiteral("preserve-session.qgz"));
    project.setDirty(true);
    connect(
        &project, &QgsProject::writeProject, &project,
        [](QDomDocument&) { throw std::runtime_error("synthetic serialization failure"); }, Qt::DirectConnection);
    QVERIFY(!kaWriteQgisProjectAtomic(&project, path, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(fileHash(path), before);
    QCOMPARE(project.fileName(), QStringLiteral("preserve-session.qgz"));
    QVERIFY(project.isDirty());
    QVERIFY(temporaryArtifacts(dir.path()).isEmpty());
  }

  void recoverySnapshot_layerFilterSkipsUnlistedVectors() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QgsProject project;
    auto* keep = new QgsVectorLayer(QStringLiteral("Point?crs=EPSG:5186&field=note:string"),
                                    QStringLiteral("남길점"), QStringLiteral("memory"));
    auto* skip = new QgsVectorLayer(QStringLiteral("Point?crs=EPSG:5186&field=note:string"),
                                    QStringLiteral("뺄점"), QStringLiteral("memory"));
    QVERIFY(keep->isValid());
    QVERIFY(skip->isValid());
    project.addMapLayer(keep);
    project.addMapLayer(skip);
    QVERIFY(keep->startEditing());
    QgsFeature kept(keep->fields());
    kept.setAttribute(QStringLiteral("note"), QStringLiteral("a"));
    kept.setGeometry(QgsGeometry::fromWkt(QStringLiteral("Point (190000 560000)")));
    QVERIFY(keep->addFeature(kept));
    QVERIFY(skip->startEditing());
    QgsFeature dropped(skip->fields());
    dropped.setAttribute(QStringLiteral("note"), QStringLiteral("b"));
    dropped.setGeometry(QgsGeometry::fromWkt(QStringLiteral("Point (190010 560010)")));
    QVERIFY(skip->addFeature(dropped));
    const QString keepSource = keep->source();
    const QString skipSource = skip->source();
    QString error;
    const QString path = SurveyStorage::writeRecoverySnapshot(
        &project, dir.filePath(QStringLiteral("복구사본")), &error, {keep->id()});
    QVERIFY2(!path.isEmpty(), qPrintable(error));
    QCOMPARE(keep->source(), keepSource);
    QCOMPARE(skip->source(), skipSource);
    QVERIFY(keep->isModified());
    QgsProject restored;
    QVERIFY2(restored.read(SurveyStorage::projectUri(path)), qPrintable(restored.error()));
    QCOMPARE(restored.mapLayersByName(QStringLiteral("남길점")).size(), 1);
    QCOMPARE(restored.mapLayersByName(QStringLiteral("뺄점")).size(), 0);
  }

  void recoveryPending_notesNewestAndPrunesOldCopies() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.filePath(QStringLiteral("복구사본"));
    QVERIFY(QDir().mkpath(root));
    QStringList snapshots;
    for (int i = 0; i < 4; ++i) {
      const QString folder = QDir(root).filePath(QStringLiteral("조사복구_20260101_00000%1_aaaaa%1").arg(i));
      QVERIFY(QDir().mkpath(folder));
      const QString snapshot = QDir(folder).filePath(QStringLiteral("복구조사.gpkg"));
      QFile file(snapshot);
      QVERIFY(file.open(QIODevice::ReadWrite));
      QCOMPARE(file.write("x"), static_cast<qint64>(1)); QVERIFY(file.flush());  // 닫을 때 버퍼가 비워지며 수정 시각이 덮이지 않게
      QVERIFY(file.setFileTime(QDateTime(QDate(2026, 1, 1), QTime(0, i, 0)),
                               QFileDevice::FileModificationTime));
      file.close();
      snapshots << snapshot;
    }
    QString error;
    QVERIFY2(SurveyStorage::noteRecoveryPending(root, snapshots.last(), &error), qPrintable(error));
    QCOMPARE(SurveyStorage::pendingRecoverySnapshot(root),
             QDir::cleanPath(QFileInfo(snapshots.last()).absoluteFilePath()));
    QVERIFY(SurveyStorage::pruneRecoverySnapshots(root, 3, snapshots.last()) >= 1);
    int left = 0;
    for (const QString& snapshot : snapshots)
      if (QFileInfo::exists(snapshot)) ++left;
    QCOMPARE(left, 3);
    QVERIFY(QFileInfo::exists(snapshots.last()));
    QVERIFY(!QFileInfo::exists(snapshots.first()));
    SurveyStorage::clearRecoveryPending(root);
    QVERIFY(SurveyStorage::pendingRecoverySnapshot(root).isEmpty());
  }

  void absorbLeavesReferenceVectorsOutsideTheSurvey() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString gpkg = SurveyProjectFactory::createNewSurvey(
        dir.path(), QStringLiteral("참조분리"), &error, QStringLiteral("EPSG:5186"));
    QVERIFY2(!gpkg.isEmpty(), qPrintable(error));
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5186")));
    auto* scratch = new QgsVectorLayer(
        QStringLiteral("Polygon?crs=EPSG:5186&field=name:string(20)"),
        QStringLiteral("수치지형도"), QStringLiteral("memory"));
    QVERIFY(scratch->isValid());
    QgsFeature contour(scratch->fields());
    contour.setAttribute(0, QStringLiteral("등고"));
    contour.setGeometry(QgsGeometry::fromRect(QgsRectangle(0, 0, 10, 10)));
    QVERIFY(scratch->dataProvider()->addFeature(contour));
    const QString topoPath = QDir(dir.path()).filePath(QStringLiteral("수치지형도.gpkg"));
    QgsVectorFileWriter::SaveVectorOptions opt;
    opt.driverName = QStringLiteral("GPKG");
    opt.layerName = QStringLiteral("수치지형도");
    opt.fileEncoding = QStringLiteral("UTF-8");
    QString writeError;
    const QgsVectorFileWriter::WriterError code = QgsVectorFileWriter::writeAsVectorFormatV3(
        scratch, topoPath, project.transformContext(), opt, &writeError);
    QVERIFY2(code == QgsVectorFileWriter::NoError, qPrintable(writeError));
    delete scratch;
    auto* reference = new QgsVectorLayer(
        QStringLiteral("%1|layername=수치지형도").arg(topoPath),
        QStringLiteral("수치지형도"), QStringLiteral("ogr"));
    QVERIFY(reference->isValid());
    LayerOps::markReferenceLayer(reference);
    project.addMapLayer(reference);
    auto* memoryNote = new QgsVectorLayer(
        QStringLiteral("Point?crs=EPSG:5186&field=note:string"),
        QStringLiteral("현장참고점"), QStringLiteral("memory"));
    QVERIFY(memoryNote->isValid());
    LayerOps::markReferenceLayer(memoryNote);
    QgsFeature note(memoryNote->fields());
    note.setAttribute(0, QStringLiteral("다시 볼 점"));
    note.setGeometry(QgsGeometry::fromPointXY(QgsPointXY(1, 2)));
    QVERIFY(memoryNote->dataProvider()->addFeature(note));
    project.addMapLayer(memoryNote);
    auto* brought = new QgsVectorLayer(
        QStringLiteral("Polygon?crs=EPSG:5186&field=name:string(20)"),
        QStringLiteral("가져온면"), QStringLiteral("memory"));
    QVERIFY(brought->isValid());
    QgsFeature extra(brought->fields());
    extra.setGeometry(QgsGeometry::fromRect(QgsRectangle(1, 1, 2, 2)));
    QVERIFY(brought->dataProvider()->addFeature(extra));
    project.addMapLayer(brought);
    const SurveyStorage::AbsorbResult result = SurveyStorage::absorbExternalVectors(&project, gpkg);
    QVERIFY(result.imported.contains(QStringLiteral("가져온면")));
    QVERIFY(result.imported.contains(QStringLiteral("현장참고점")));
    QVERIFY(!result.imported.contains(QStringLiteral("수치지형도")));
    QVERIFY(result.skippedReference.contains(QStringLiteral("수치지형도")));
    QVERIFY(!result.skippedReference.contains(QStringLiteral("현장참고점")));
  }

  void persistWorkspace_secondLayerCommitFails_keepsEditsAndRecovery() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString gpkg = SurveyProjectFactory::createNewSurvey(
        dir.path(), QStringLiteral("부분커밋"), &error, QStringLiteral("EPSG:5186"));
    QVERIFY2(!gpkg.isEmpty(), qPrintable(error));
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5186")));
    auto* area = LayerOps::ensureDomainLayer(&project, gpkg, QStringLiteral("survey_area"),
                                             QStringLiteral("조사구역"), &error);
    auto* line = LayerOps::ensureDomainLayer(&project, gpkg, QStringLiteral("feature_line"),
                                             QStringLiteral("유구선"), &error);
    QVERIFY2(area && line, qPrintable(error));
    QVERIFY(area->startEditing());
    QgsFeature areaFeature(area->fields());
    areaFeature.setAttribute(QStringLiteral("note"), QStringLiteral("첫 레이어 면"));
    areaFeature.setGeometry(QgsGeometry::fromRect(QgsRectangle(200000, 450000, 200010, 450010)));
    QVERIFY(area->addFeature(areaFeature));
    QVERIFY(line->startEditing());
    QgsFeature lineFeature(line->fields());
    lineFeature.setAttribute(QStringLiteral("note"), QStringLiteral("둘째 레이어 선"));
    lineFeature.setGeometry(QgsGeometry::fromWkt(QStringLiteral("LineString (200000 450000, 200020 450020)")));
    QVERIFY(line->addFeature(lineFeature));
    line->setAllowCommit(false);
    project.setDirty(true);
    const QString recovery = dir.filePath(QStringLiteral("복구사본"));
    const auto attempt = SurveyStorage::persistWorkspace(&project, gpkg, recovery);
    QVERIFY(!attempt.saved);
    QVERIFY(attempt.failedLayers.contains(QStringLiteral("유구선")));
    QVERIFY2(!attempt.recoveryPath.isEmpty(), qPrintable(attempt.error));
    QVERIFY(QFileInfo::exists(attempt.recoveryPath));
    QVERIFY(line->isEditable() && line->isModified());
    QCOMPARE(line->featureCount(), 1LL);
    QVERIFY(project.isDirty());
    QgsProject restored;
    QVERIFY2(restored.read(SurveyStorage::projectUri(attempt.recoveryPath)), qPrintable(restored.error()));
    const auto recoveredLine = restored.mapLayersByName(QStringLiteral("유구선"));
    QCOMPARE(recoveredLine.size(), 1);
    QCOMPARE(qobject_cast<QgsVectorLayer*>(recoveredLine.first())->featureCount(), 1LL);
    line->setAllowCommit(true);
    line->rollBack();
    if (area->isModified()) area->rollBack();
  }

  void persistWorkspace_lockedGpkg_keepsEditsAndRecovery() {
#ifdef Q_OS_WIN
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString gpkg = SurveyProjectFactory::createNewSurvey(
        dir.path(), QStringLiteral("잠긴조사"), &error, QStringLiteral("EPSG:5187"));
    QVERIFY2(!gpkg.isEmpty(), qPrintable(error));
    const QByteArray before = fileHash(gpkg);
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5187")));
    auto* scratch = new QgsVectorLayer(
        QStringLiteral("Polygon?crs=EPSG:5187&field=note:string"),
        QStringLiteral("가져온면"), QStringLiteral("memory"));
    QVERIFY(scratch->isValid());
    QVERIFY(scratch->startEditing());
    QgsFeature extra(scratch->fields());
    extra.setAttribute(0, QStringLiteral("잠금 중에도 남을 면"));
    extra.setGeometry(QgsGeometry::fromRect(QgsRectangle(190000, 560000, 190010, 560010)));
    QVERIFY(scratch->addFeature(extra));
    project.addMapLayer(scratch);
    project.setDirty(true);
    ReadLock lock(gpkg);
    QVERIFY(lock.valid());
    const auto attempt = SurveyStorage::persistWorkspace(
        &project, gpkg, dir.filePath(QStringLiteral("복구사본")));
    QVERIFY2(attempt.saved, qPrintable(attempt.error));
    QCOMPARE(fileHash(gpkg), before);
    const QString saved = attempt.surveyPath.isEmpty() ? gpkg : attempt.surveyPath;
    QVERIFY(QFileInfo::exists(saved));
    QVERIFY(saved.compare(QFileInfo(gpkg).absoluteFilePath(), Qt::CaseInsensitive) != 0);
    QVERIFY(saved.contains(QStringLiteral("-저장")));
    QgsVectorLayer disk(saved + QStringLiteral("|layername=가져온면"), QStringLiteral("disk"),
                        QStringLiteral("ogr"));
    QVERIFY2(disk.isValid(), qPrintable(saved));
    QCOMPARE(disk.featureCount(), 1LL);
    QVERIFY(scratch->isValid());
    QCOMPARE(scratch->featureCount(), 1LL);
#else
    QSKIP("Windows sharing-lock failure contract");
#endif
  }

  void persistWorkspace_missingExternal_keepsEditsAndRecovery() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString gpkg = SurveyProjectFactory::createNewSurvey(
        dir.path(), QStringLiteral("외부누락"), &error, QStringLiteral("EPSG:5186"));
    QVERIFY2(!gpkg.isEmpty(), qPrintable(error));
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5186")));
    auto* area = LayerOps::ensureDomainLayer(&project, gpkg, QStringLiteral("survey_area"),
                                             QStringLiteral("조사구역"), &error);
    QVERIFY2(area, qPrintable(error));
    QVERIFY(area->startEditing());
    QgsFeature areaFeature(area->fields());
    areaFeature.setAttribute(QStringLiteral("note"), QStringLiteral("조사 면"));
    areaFeature.setGeometry(QgsGeometry::fromRect(QgsRectangle(200000, 450000, 200010, 450010)));
    QVERIFY(area->addFeature(areaFeature));
    QgsVectorLayer source(
        QStringLiteral("Polygon?crs=EPSG:5186&field=name:string"),
        QStringLiteral("fixture"), QStringLiteral("memory"));
    QgsFeature boundary(source.fields());
    boundary.setAttribute(0, QStringLiteral("외부경계"));
    boundary.setGeometry(QgsGeometry::fromRect(QgsRectangle(199900, 449900, 200100, 450100)));
    QVERIFY(source.isValid() && source.dataProvider()->addFeature(boundary));
    const QString shp = QDir(dir.path()).filePath(QStringLiteral("외부경계.shp"));
    QgsVectorFileWriter::SaveVectorOptions options;
    options.driverName = QStringLiteral("ESRI Shapefile");
    options.fileEncoding = QStringLiteral("UTF-8");
    QString writeError;
    QVERIFY2(QgsVectorFileWriter::writeAsVectorFormatV3(&source, shp, project.transformContext(),
                                                        options, &writeError) == QgsVectorFileWriter::NoError,
             qPrintable(writeError));
    {
      QgsVectorLayer probe(shp, QStringLiteral("외부경계"), QStringLiteral("ogr"));
      QVERIFY(probe.isValid());
    }
    const QString base = QDir(dir.path()).filePath(QStringLiteral("외부경계"));
    for (const QString& ext : {QStringLiteral(".shp"), QStringLiteral(".shx"), QStringLiteral(".dbf"),
                               QStringLiteral(".prj"), QStringLiteral(".cpg"), QStringLiteral(".qpj")}) {
      const QString sidecar = base + ext;
      if (QFile::exists(sidecar))
        QVERIFY2(QFile::remove(sidecar), qPrintable(sidecar));
    }
    QVERIFY(!QFileInfo::exists(shp));
    auto* external = new QgsVectorLayer(shp, QStringLiteral("외부경계"), QStringLiteral("ogr"));
    QVERIFY(!external->isValid());
    QVERIFY(!LayerOps::isReferenceLayer(external));
    project.addMapLayer(external);
    project.setDirty(true);
    const auto attempt = SurveyStorage::persistWorkspace(
        &project, gpkg, dir.filePath(QStringLiteral("복구사본")));
    QVERIFY(!attempt.saved);
    QVERIFY(attempt.failedLayers.contains(QStringLiteral("외부경계")));
    QVERIFY2(!attempt.recoveryPath.isEmpty(), qPrintable(attempt.error));
    QVERIFY(QFileInfo::exists(attempt.recoveryPath));
    QCOMPARE(area->featureCount(), 1LL);
    QVERIFY(project.isDirty());
    QgsProject restored;
    QVERIFY2(restored.read(SurveyStorage::projectUri(attempt.recoveryPath)), qPrintable(restored.error()));
    const auto recoveredArea = restored.mapLayersByName(QStringLiteral("조사구역"));
    QCOMPARE(recoveredArea.size(), 1);
    QCOMPARE(qobject_cast<QgsVectorLayer*>(recoveredArea.first())->featureCount(), 1LL);
  }

  void persistWorkspace_writeExceptionKeepsPreviousGeneration() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString gpkg = SurveyProjectFactory::createNewSurvey(
        dir.path(), QStringLiteral("세대저장"), &error, QStringLiteral("EPSG:5186"));
    QVERIFY2(!gpkg.isEmpty(), qPrintable(error));
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5186")));
    auto* area = LayerOps::ensureDomainLayer(&project, gpkg, QStringLiteral("survey_area"),
                                             QStringLiteral("조사구역"), &error);
    QVERIFY2(area, qPrintable(error));
    QVERIFY(area->startEditing());
    QgsFeature first(area->fields());
    first.setAttribute(QStringLiteral("note"), QStringLiteral("이전 세대"));
    first.setGeometry(QgsGeometry::fromRect(QgsRectangle(200000, 450000, 200010, 450010)));
    QVERIFY(area->addFeature(first));
    const QString recovery = dir.filePath(QStringLiteral("복구사본"));
    const auto firstSave = SurveyStorage::persistWorkspace(&project, gpkg, recovery);
    QVERIFY2(firstSave.saved, qPrintable(firstSave.error));
    QVERIFY(SurveyStorage::validateForOpen(gpkg, &error));
    {
      QgsVectorLayer disk(gpkg + QStringLiteral("|layername=survey_area"), QStringLiteral("disk"),
                          QStringLiteral("ogr"));
      QVERIFY(disk.isValid());
      QCOMPARE(disk.featureCount(), 1LL);
    }
    const QByteArray before = fileHash(gpkg);
    QVERIFY(!before.isEmpty());
    if (!area->isEditable()) QVERIFY(area->startEditing());
    QgsFeature second(area->fields());
    second.setAttribute(QStringLiteral("note"), QStringLiteral("저장되면 안 되는 면"));
    second.setGeometry(QgsGeometry::fromRect(QgsRectangle(200020, 450020, 200030, 450030)));
    QVERIFY(area->addFeature(second));
    QVERIFY(area->isModified());
    QCOMPARE(area->featureCount(), 2LL);
    project.setDirty(true);
    connect(
        &project, &QgsProject::writeProject, &project,
        [](QDomDocument&) { throw std::runtime_error("synthetic generation write failure"); },
        Qt::DirectConnection);
    const auto attempt = SurveyStorage::persistWorkspace(&project, gpkg, recovery);
    QVERIFY(!attempt.saved);
    QCOMPARE(fileHash(gpkg), before);
    QVERIFY2(SurveyStorage::validateForOpen(gpkg, &error), qPrintable(error));
    {
      QgsVectorLayer disk(gpkg + QStringLiteral("|layername=survey_area"), QStringLiteral("disk"),
                          QStringLiteral("ogr"));
      QVERIFY(disk.isValid());
      QCOMPARE(disk.featureCount(), 1LL);
    }
    QVERIFY(area->isModified());
    QCOMPARE(area->featureCount(), 2LL);
    QVERIFY2(!attempt.recoveryPath.isEmpty(), qPrintable(attempt.error));
  }

  void persistWork_writesCompanionQgzWithoutMainWindow() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString gpkg = SurveyProjectFactory::createNewSurvey(
        dir.path(), QStringLiteral("세션저장"), &error, QStringLiteral("EPSG:5186"));
    QVERIFY2(!gpkg.isEmpty(), qPrintable(error));
    QVERIFY(!gpkg.contains(QStringLiteral("제주")));
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5186")));
    auto* area = LayerOps::ensureDomainLayer(&project, gpkg, QStringLiteral("survey_area"),
                                             QStringLiteral("조사구역"), &error);
    QVERIFY2(area, qPrintable(error));
    QVERIFY(area->startEditing());
    QgsFeature first(area->fields());
    first.setAttribute(QStringLiteral("note"), QStringLiteral("세션면"));
    first.setGeometry(QgsGeometry::fromRect(QgsRectangle(200000, 450000, 200010, 450010)));
    QVERIFY(area->addFeature(first));
    SurveySession::PersistInput in;
    in.surveyPath = gpkg;
    in.recoveryDirectory = dir.filePath(QStringLiteral("복구사본"));
    const auto result = SurveySession::persistWork(&project, in);
    QVERIFY2(result.saved, qPrintable(result.workspace.error));
    QVERIFY(result.companionSaved);
    QCOMPARE(result.companionQgzPath, SurveySession::companionQgzPathFor(gpkg));
    QVERIFY(QFileInfo::exists(result.companionQgzPath));
    QVERIFY2(SurveyStorage::validateForOpen(gpkg, &error), qPrintable(error));
    QgsVectorLayer disk(gpkg + QStringLiteral("|layername=survey_area"), QStringLiteral("disk"),
                        QStringLiteral("ogr"));
    QVERIFY(disk.isValid());
    QCOMPARE(disk.featureCount(), 1LL);
  }

  void persistWorkspace_missingSurveyFile_writesLocalCopy() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString gpkg = QDir(dir.path()).filePath(QStringLiteral("떠난조사.gpkg"));
    QVERIFY(!QFileInfo::exists(gpkg));
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5186")));
    auto* area = new QgsVectorLayer(
        QStringLiteral("Polygon?crs=EPSG:5186&field=note:string"),
        QStringLiteral("조사구역"), QStringLiteral("memory"));
    QVERIFY(area->isValid());
    QVERIFY(area->startEditing());
    QgsFeature first(area->fields());
    first.setAttribute(0, QStringLiteral("다른PC면"));
    first.setGeometry(QgsGeometry::fromRect(QgsRectangle(200000, 450000, 200010, 450010)));
    QVERIFY(area->addFeature(first));
    project.addMapLayer(area);
    QCOMPARE(SurveyStorage::writableSurveyPath(gpkg, dir.path()), QFileInfo(gpkg).absoluteFilePath());
    const QString missingParent = QDir(dir.path()).filePath(QStringLiteral("없는폴더/떠난조사.gpkg"));
    const QString relocated = SurveyStorage::writableSurveyPath(missingParent, dir.path());
    QCOMPARE(QFileInfo(relocated).fileName(), QStringLiteral("떠난조사.gpkg"));
    QVERIFY(relocated.startsWith(QDir(dir.path()).absolutePath(), Qt::CaseInsensitive));
    const auto attempt = SurveyStorage::persistWorkspace(&project, gpkg, dir.filePath(QStringLiteral("복구사본")));
    QVERIFY2(attempt.saved, qPrintable(attempt.error));
    QVERIFY(QFileInfo::exists(gpkg));
  }

  void persistWorkspace_staleWalDoesNotBlockSave() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString gpkg = SurveyProjectFactory::createNewSurvey(
        dir.path(), QStringLiteral("열린조사"), &error, QStringLiteral("EPSG:5186"));
    QVERIFY2(!gpkg.isEmpty(), qPrintable(error));
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5186")));
    auto* area = LayerOps::ensureDomainLayer(&project, gpkg, QStringLiteral("survey_area"),
                                             QStringLiteral("조사구역"), &error);
    QVERIFY2(area, qPrintable(error));
    QVERIFY(area->startEditing());
    QgsFeature first(area->fields());
    first.setAttribute(QStringLiteral("note"), QStringLiteral("WAL"));
    first.setGeometry(QgsGeometry::fromRect(QgsRectangle(200000, 450000, 200010, 450010)));
    QVERIFY(area->addFeature(first));
    const QIODevice::OpenMode keep = QSysInfo::productType() == QLatin1String("windows") ? QIODevice::NotOpen : QIODevice::Append;  // 리눅스는 열린 -shm 을 비우면 SIGBUS
    for (const char* suffix : {"-wal", "-shm"}) QVERIFY(QFile(gpkg + QLatin1String(suffix)).open(QIODevice::WriteOnly | keep));
    const auto attempt = SurveyStorage::persistWorkspace(
        &project, gpkg, dir.filePath(QStringLiteral("복구사본")));
    QVERIFY2(attempt.saved, qPrintable(attempt.error));
    QVERIFY(!attempt.error.contains(QStringLiteral("사용 중")));
    QVERIFY(QFileInfo::exists(attempt.surveyPath.isEmpty() ? gpkg : attempt.surveyPath));
  }

  void persistWork_missingParentRelocatesToFallback() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5186")));
    auto* area = new QgsVectorLayer(
        QStringLiteral("Polygon?crs=EPSG:5186&field=note:string"),
        QStringLiteral("조사구역"), QStringLiteral("memory"));
    QVERIFY(area->isValid());
    QVERIFY(area->startEditing());
    QgsFeature first(area->fields());
    first.setAttribute(0, QStringLiteral("이전"));
    first.setGeometry(QgsGeometry::fromRect(QgsRectangle(200000, 450000, 200010, 450010)));
    QVERIFY(area->addFeature(first));
    project.addMapLayer(area);
    SurveySession::PersistInput in;
    in.surveyPath = QDir(dir.path()).filePath(QStringLiteral("없는곳/조사.gpkg"));
    in.fallbackDirectory = dir.path();
    in.writeCompanionQgz = false;
    const auto result = SurveySession::persistWork(&project, in);
    QVERIFY2(result.saved, qPrintable(result.workspace.error));
    QCOMPARE(QFileInfo(result.surveyPath).fileName(), QStringLiteral("조사.gpkg"));
    QVERIFY(QFileInfo::exists(result.surveyPath));
  }

  void persistWork_emptyPathDoesNotWrite() {
    QgsProject project;
    SurveySession::PersistInput in;
    const auto result = SurveySession::persistWork(&project, in);
    QVERIFY(!result.saved);
    QVERIFY(result.workspace.error.contains(QStringLiteral("조사 경로")));
  }

  void extractEmbeddedReferenceVectors_shrinksSurveyAndKeepsDomain() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString gpkg = SurveyProjectFactory::createNewSurvey(
        dir.path(), QStringLiteral("참조추출"), &error, QStringLiteral("EPSG:5186"));
    QVERIFY2(!gpkg.isEmpty(), qPrintable(error));
    QVERIFY(!gpkg.contains(QStringLiteral("제주")));
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5186")));
    auto* area = LayerOps::ensureDomainLayer(&project, gpkg, QStringLiteral("survey_area"),
                                             QStringLiteral("조사구역"), &error);
    QVERIFY2(area, qPrintable(error));
    QVERIFY(area->startEditing());
    QgsFeature first(area->fields());
    first.setAttribute(QStringLiteral("note"), QStringLiteral("조사면"));
    first.setGeometry(QgsGeometry::fromRect(QgsRectangle(200000, 450000, 200010, 450010)));
    QVERIFY(area->addFeature(first));
    const QString recovery = dir.filePath(QStringLiteral("복구사본"));
    const auto firstSave = SurveyStorage::persistWorkspace(&project, gpkg, recovery);
    QVERIFY2(firstSave.saved, qPrintable(firstSave.error));

    QgsVectorLayer memory(QStringLiteral("Polygon?crs=EPSG:5186"), QStringLiteral("외부경계"),
                          QStringLiteral("memory"));
    QVERIFY(memory.isValid());
    QVERIFY(memory.startEditing());
    for (int i = 0; i < 4000; ++i) {
      QgsFeature feature(memory.fields());
      feature.setGeometry(QgsGeometry::fromRect(
          QgsRectangle(201000 + i, 451000, 201001 + i, 451001)));
      QVERIFY(memory.addFeature(feature));
    }
    QVERIFY(memory.commitChanges());
    QgsVectorFileWriter::SaveVectorOptions opt;
    opt.driverName = QStringLiteral("GPKG");
    opt.layerName = QStringLiteral("외부경계");
    opt.fileEncoding = QStringLiteral("UTF-8");
    opt.actionOnExistingFile = QgsVectorFileWriter::CreateOrOverwriteLayer;
    QString writeError;
    QCOMPARE(QgsVectorFileWriter::writeAsVectorFormatV3(
                 &memory, gpkg, project.transformContext(), opt, &writeError),
             QgsVectorFileWriter::NoError);
    auto* reference = new QgsVectorLayer(gpkg + QStringLiteral("|layername=외부경계"),
                                         QStringLiteral("외부경계"), QStringLiteral("ogr"));
    QVERIFY(reference->isValid());
    QCOMPARE(reference->featureCount(), 4000LL);
    project.addMapLayer(reference);
    LayerOps::markReferenceLayer(reference);
    LayerOps::markReferenceLayer(area);
    const QStringList names = SurveyStorage::embeddedReferenceVectorNames(&project, gpkg);
    QCOMPARE(names, QStringList{QStringLiteral("외부경계")});
    QVERIFY(!names.contains(QStringLiteral("조사구역")));
    const qint64 before = QFileInfo(gpkg).size();
    QVERIFY(before > 0);
    const QString output = dir.filePath(QStringLiteral("참조지도"));
    const auto attempt = SurveyStorage::extractEmbeddedReferenceVectors(&project, gpkg, output);
    QVERIFY2(attempt.extracted, qPrintable(attempt.error));
    QCOMPARE(attempt.moved, QStringList{QStringLiteral("외부경계")});
    QVERIFY(attempt.failed.isEmpty());
    QVERIFY2(attempt.bytesAfter > 0 && attempt.bytesAfter < before,
             qPrintable(QStringLiteral("before=%1 after=%2").arg(before).arg(attempt.bytesAfter)));
    QVERIFY(QDir(output).exists());
    QVERIFY(!QDir(output).entryList(QStringList{QStringLiteral("*.gpkg")}, QDir::Files).isEmpty());
    QVERIFY(reference->isValid());
    QVERIFY(!reference->source().contains(QFileInfo(gpkg).absoluteFilePath(), Qt::CaseInsensitive));
    QCOMPARE(reference->featureCount(), 4000LL);
    QVERIFY(area->isValid());
    QCOMPARE(area->featureCount(), 1LL);
    {
      QgsVectorLayer domain(gpkg + QStringLiteral("|layername=survey_area"), QStringLiteral("disk"),
                            QStringLiteral("ogr"));
      QVERIFY(domain.isValid());
      QCOMPARE(domain.featureCount(), 1LL);
    }
    {
      QgsVectorLayer leftover(gpkg + QStringLiteral("|layername=외부경계"), QStringLiteral("gone"),
                              QStringLiteral("ogr"));
      QVERIFY(!leftover.isValid() || leftover.featureCount() == 0);
    }
    QVERIFY(SurveyStorage::embeddedReferenceVectorNames(&project, gpkg).isEmpty());
  }

  void repairPersisted_restoresDroppedWindowsHome() {
    const QString localRoot = QDir::home().filePath(QStringLiteral("AppData/Local"));
    if (!QDir(localRoot).exists())
      QSKIP("Windows AppData/Local 이 없습니다.");
    QTemporaryDir cache(QDir(localRoot).filePath(QStringLiteral("ka-hgis-rpr-XXXXXX")));
    QVERIFY2(cache.isValid(), qPrintable(cache.path()));
    QTemporaryDir surveyDir;
    QVERIFY(surveyDir.isValid());
    QString error;
    const QString gpkg = SurveyProjectFactory::createNewSurvey(
        surveyDir.path(), QStringLiteral("경로복구"), &error, QStringLiteral("EPSG:5186"));
    QVERIFY2(!gpkg.isEmpty(), qPrintable(error));
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5186")));
    project.setFileName(gpkg);
    project.setPresetHomePath(QFileInfo(gpkg).absolutePath());
    const QString dest = QDir(cache.path()).filePath(QStringLiteral("heritage.gpkg"));
    const QString source = writeHeritageGpkg(dest, &project, &error);
    QVERIFY2(!source.isEmpty(), qPrintable(error));
    const QString good = QDir::fromNativeSeparators(QFileInfo(dest).absoluteFilePath());
    // The saved path lost the account folder: <profiles>/<user>/AppData → <profiles>/AppData.
    // Build it from this PC's real profile location instead of assuming C:/Users.
    const QString home = QDir::fromNativeSeparators(QFileInfo(QDir::homePath()).absoluteFilePath());
    const QString profiles = QFileInfo(home).path();
    if (!profiles.endsWith(QStringLiteral("/Users"), Qt::CaseInsensitive))
      QSKIP("이 복구는 <드라이브>:/Users/<계정> 형태의 프로필에만 해당합니다.");
    QVERIFY(good.startsWith(home + QLatin1Char('/'), Qt::CaseInsensitive));
    const QString broken = profiles + good.mid(home.size());
    QVERIFY(broken.startsWith(profiles + QStringLiteral("/AppData/"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(broken));
    auto* layer = new QgsVectorLayer(broken + QStringLiteral("|layername=heritage"),
                                     QStringLiteral("국가지정유산"), QStringLiteral("ogr"));
    QVERIFY(!layer->isValid());
    LayerOps::markReferenceLayer(layer);
    project.addMapLayer(layer);
    QCOMPARE(LayerOps::repairPersistedFileSources(&project), 1);
    QVERIFY(layer->isValid());
    QCOMPARE(layer->featureCount(), 1LL);
    QVERIFY(QDir::fromNativeSeparators(layer->source()).startsWith(good, Qt::CaseInsensitive));
  }

  void persistWorkspace_keepsExternalHeritagePathOnReopen() {
    const QString localRoot = QDir::home().filePath(QStringLiteral("AppData/Local"));
    if (!QDir(localRoot).exists())
      QSKIP("Windows AppData/Local 이 없습니다.");
    QTemporaryDir cache(QDir(localRoot).filePath(QStringLiteral("ka-hgis-pst-XXXXXX")));
    QVERIFY2(cache.isValid(), qPrintable(cache.path()));
    QTemporaryDir surveyDir;
    QVERIFY(surveyDir.isValid());
    QString error;
    const QString gpkg = SurveyProjectFactory::createNewSurvey(
        surveyDir.path(), QStringLiteral("참조유지"), &error, QStringLiteral("EPSG:5186"));
    QVERIFY2(!gpkg.isEmpty(), qPrintable(error));
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5186")));
    auto* area = LayerOps::ensureDomainLayer(&project, gpkg, QStringLiteral("survey_area"),
                                             QStringLiteral("조사구역"), &error);
    QVERIFY2(area, qPrintable(error));
    const QString dest = QDir(cache.path()).filePath(QStringLiteral("heritage.gpkg"));
    const QString source = writeHeritageGpkg(dest, &project, &error);
    QVERIFY2(!source.isEmpty(), qPrintable(error));
    auto* reference = new QgsVectorLayer(source, QStringLiteral("국가지정유산"), QStringLiteral("ogr"));
    QVERIFY(reference->isValid());
    LayerOps::markReferenceLayer(reference);
    project.addMapLayer(reference);
    LayerOps::placeInLegendGroup(&project, reference, QString::fromUtf8(LayerOps::kGroupReference));
    const auto attempt = SurveyStorage::persistWorkspace(&project, gpkg, surveyDir.filePath(QStringLiteral("복구")));
    QVERIFY2(attempt.saved, qPrintable(attempt.error));
    QgsProject reopened;
    QVERIFY2(SurveyStorage::readEmbedded(&reopened, gpkg), qPrintable(reopened.error()));
    QCOMPARE(LayerOps::repairPersistedFileSources(&reopened), 0);
    const auto layers = reopened.mapLayersByName(QStringLiteral("국가지정유산"));
    QCOMPARE(layers.size(), 1);
    auto* restored = qobject_cast<QgsVectorLayer*>(layers.first());
    QVERIFY(restored && restored->isValid());
    QCOMPARE(restored->featureCount(), 1LL);
    const QString restoredFile =
        QDir::fromNativeSeparators(restored->source().section(QLatin1Char('|'), 0, 0));
    QVERIFY2(!restoredFile.contains(QStringLiteral("/Users/AppData/"), Qt::CaseInsensitive),
             qPrintable(restoredFile));
    QVERIFY(QFileInfo::exists(restoredFile));
  }
};

int main(int argc, char** argv) {
  // Isolate everything the storage code writes outside the per-test temp folders before QGIS
  // starts: user settings (recent list), the session log and the unsafe-project list
  // (both under LOCALAPPDATA/ka-hgis) and QStandardPaths app folders. Same order as test_perf.
  QStandardPaths::setTestModeEnabled(true);
  QTemporaryDir isolated;
  if (!isolated.isValid()) return 1;
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolated.filePath(QStringLiteral("settings")));
  qputenv("KA_HGIS_LOG_DIR", QDir::toNativeSeparators(isolated.filePath(QStringLiteral("logs"))).toLocal8Bit());
  qputenv("LOCALAPPDATA", QDir::toNativeSeparators(isolated.filePath(QStringLiteral("local"))).toLocal8Bit());
  QgsApplication app(argc, argv, true);
  QgsApplication::setPrefixPath(qEnvironmentVariable("QGIS_PREFIX_PATH"), true);
  QgsApplication::initQgis();
  TestStorageSafety tests;
  const int result = QTest::qExec(&tests, argc, argv);
  QgsApplication::exitQgis();
  return result;
}

#include "test_storage_safety.moc"

