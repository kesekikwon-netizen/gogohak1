#include "HeritageLayoutNumbers.h"
#include "HeritageStyle.h"
#include "KaLogExcept.h"
#include "LayerFeatures.h"
#include "LayerOps.h"
#include "NearbyHeritageExport.h"
#include "NearbyHeritageFields.h"

#include <algorithm>
#include <cmath>
#include <QMap>
#include <QSet>
#include <qgscoordinatetransform.h>
#include <qgsfeatureiterator.h>
#include <qgsgeometry.h>
#include <qgslayertree.h>
#include <qgslayertreelayer.h>
#include <qgslinestring.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>

namespace NearbyHeritageExport {
namespace {

QgsGeometry polygonsOnly(const QgsGeometry& g) {
  if (g.isEmpty() || g.type() == Qgis::GeometryType::Polygon) return g;
  QVector<QgsGeometry> polys;
  for (const QgsGeometry& part : g.asGeometryCollection())
    if (part.type() == Qgis::GeometryType::Polygon) polys << part;
  return polys.isEmpty() ? QgsGeometry() : QgsGeometry::collectGeometry(polys);
}

QgsGeometry unionPolygonsInCrs(QgsVectorLayer* source, const QgsCoordinateReferenceSystem& dest, QgsProject* project,
                               QString* errorOut) {
  const bool needXf = source->crs().isValid() && dest.isValid() && source->crs() != dest;
  QgsCoordinateTransform xf(source->crs(), dest,
                            project ? project->transformContext() : QgsCoordinateTransformContext());
  if (needXf) xf.setBallparkTransformsAreAppropriate(true);
  QgsFeatureIterator it = source->getFeatures();
  QgsFeature f;
  QVector<QgsGeometry> parts;
  while (it.nextFeature(f)) {
    QgsGeometry g = f.geometry();
    if (g.isEmpty()) continue;
    if (needXf) {
      try {
        if (g.transform(xf) != Qgis::GeometryOperationResult::Success) continue;
      } catch (...) {
        KA_LOG_EXCEPT();
        continue;
      }
    }
    parts << g;
  }
  QgsGeometry poly = polygonsOnly(parts.isEmpty() ? QgsGeometry() : QgsGeometry::unaryUnion(parts));
  if (poly.isEmpty()) {
    if (errorOut) *errorOut = QStringLiteral("조사구역 면 도형이 없습니다. 조사구역을 먼저 그리세요.");
    return {};
  }
  poly.removeDuplicateNodes(0.05);
  const QgsGeometry valid = polygonsOnly(poly.makeValid());
  return valid.isEmpty() ? poly : valid;
}

std::optional<HeritageDataset> datasetForLayer(const QgsProject* project, const QgsVectorLayer* layer) {
  if (!layer) return std::nullopt;
  if (auto tagged = HeritageLayoutNumbers::taggedDataset(layer)) return tagged;
  if (project && project->layerTreeRoot())
    for (QgsLayerTreeNode* node = project->layerTreeRoot()->findLayer(layer->id()); node; node = node->parent())
      if (auto ds = HeritageStyle::fromLayerName(node->name())) return ds;
  const QString name = layer->name().trimmed();
  if (auto ds = HeritageStyle::fromLayerName(name)) return ds;
  for (HeritageDataset ds : HeritageStyle::allDatasets())
    if (const QString base = HeritageStyle::layerName(ds); !base.isEmpty() && name.startsWith(base)) return ds;
  if (name.contains(QStringLiteral("지정유산")) || name.contains(QStringLiteral("등록문화유산")) ||
      name.contains(QStringLiteral("보호구역")))
    return HeritageDataset::DesignatedHeritage;
  return std::nullopt;
}

QList<QgsVectorLayer*> heritageLayersInProject(QgsProject* project) {
  QList<QgsVectorLayer*> out;
  if (!project) return out;
  QSet<QString> seen;
  auto tryAdd = [&](QgsMapLayer* ml) {
    auto* vl = qobject_cast<QgsVectorLayer*>(ml);
    if (!vl || !vl->isValid() || seen.contains(vl->id()) || !datasetForLayer(project, vl).has_value()) return;
    seen.insert(vl->id());
    out.append(vl);
  };
  if (project->layerTreeRoot())
    for (QgsLayerTreeLayer* node : project->layerTreeRoot()->findLayers())
      tryAdd(node->layer());
  for (QgsMapLayer* ml : project->mapLayers())
    tryAdd(ml);
  return out;
}

double computeAzimuthDeg(const QgsGeometry& sitePoly, const QgsGeometry& target) {
  const QgsGeometry line = sitePoly.shortestLine(target);
  if (const QgsLineString* ls = line.isEmpty() ? nullptr : qgsgeometry_cast<const QgsLineString*>(line.constGet());
      ls && ls->numPoints() >= 2) {
    const QgsPointXY p1(ls->startPoint().x(), ls->startPoint().y());
    const QgsPointXY p2(ls->endPoint().x(), ls->endPoint().y());
    if (p1.distance(p2) > 1e-4) return std::fmod(std::fmod(p1.azimuth(p2), 360.0) + 360.0, 360.0);
  }
  const QgsGeometry c1 = sitePoly.pointOnSurface(), c2 = target.pointOnSurface();
  return (!c1.isEmpty() && !c2.isEmpty())
             ? std::fmod(std::fmod(c1.asPoint().azimuth(c2.asPoint()), 360.0) + 360.0, 360.0)
             : 0.0;
}

void fillMetadata(Item* item, const QgsFeature& f, const Fields& fields) {
  item->heritageCode = attributeText(f, fields.code);
  item->reportName = attributeText(f, fields.report);
  item->projectName = attributeText(f, fields.project);
  item->siteAddress = attributeText(f, fields.address);
  item->institution = attributeText(f, fields.institution);
  item->era = attributeText(f, fields.era);
  item->heritagePresence = attributeText(f, fields.presence);
  item->permitNumber = attributeText(f, fields.permit);
  item->featureName = attributeText(f, fields.feature);
}

}  // namespace

QgsVectorLayer* resolveSourceLayer(QgsProject* project, QgsVectorLayer* preferred) {
  auto isUsable = [](QgsVectorLayer* vl) {
    return vl && vl->isValid() && vl->geometryType() == Qgis::GeometryType::Polygon &&
           !LayerOps::isReferenceLayer(vl) && !LayerOps::layerKeyOf(vl).startsWith(QLatin1String("user:buffer_")) &&
           LayerFeatures::any(vl);
  };
  if (isUsable(preferred)) return preferred;
  const QgsLayerTree* root = project ? project->layerTreeRoot() : nullptr;
  QgsVectorLayer* fallback = preferred;
  for (QgsVectorLayer* vl : LayerOps::surveyAreaLayers(project)) {
    if (!isUsable(vl)) continue;
    if (const QgsLayerTreeLayer* n = root ? root->findLayer(vl->id()) : nullptr; !n || n->isVisible()) return vl;
    if (fallback == preferred) fallback = vl;
  }
  return fallback;
}

double activeMaxRadiusMeters(const QgsProject* project) {
  auto* mut = const_cast<QgsProject*>(project);
  const QgsLayerTree* root = project ? project->layerTreeRoot() : nullptr;
  auto hasRing = [&](const QString& key, bool requireVisible) {
    QgsMapLayer* ml = mut ? LayerOps::findByLayerKey(mut, key) : nullptr;
    const QgsLayerTreeLayer* n = (ml && root) ? root->findLayer(ml->id()) : nullptr;
    return ml && (!requireVisible || !n || n->isVisible());
  };
  if (hasRing(QStringLiteral("user:buffer_ring_1000"), true)) return 1000.0;
  if (hasRing(QStringLiteral("user:buffer_ring_500"), true)) return 500.0;
  return hasRing(QStringLiteral("user:buffer_ring_500"), false) &&
                 !hasRing(QStringLiteral("user:buffer_ring_1000"), false)
             ? 500.0
             : 1000.0;
}

QString cardinalDirection8(double azimuthDegrees) {
  const double norm = std::fmod(std::fmod(azimuthDegrees, 360.0) + 360.0, 360.0);
  static const char* const kDirs[8] = {"북", "북동", "동", "남동", "남", "남서", "서", "북서"};
  return QString::fromUtf8(kDirs[static_cast<int>(std::floor((norm + 22.5) / 45.0)) % 8]);
}

Result collect(QgsProject* project, QgsVectorLayer* sourceLayer, double maxRadiusMeters) {
  Result out;
  if (!project) {
    out.error = QStringLiteral("열린 조사가 없습니다.");
    return out;
  }
  QgsVectorLayer* source = resolveSourceLayer(project, sourceLayer);
  if (!source || !source->isValid() || source->geometryType() != Qgis::GeometryType::Polygon) {
    out.error = QStringLiteral("조사구역 면 레이어를 먼저 그리거나 선택하세요.");
    return out;
  }
  const QList<QgsVectorLayer*> heritageLayers = heritageLayersInProject(project);
  if (heritageLayers.isEmpty()) {
    out.error = QStringLiteral("지도에 주변유적(유산) 레이어가 없습니다. 먼저 「유산」을 눌러 자료를 받아 오세요.");
    return out;
  }
  out.maxRadiusMeters = (maxRadiusMeters >= 1.0) ? maxRadiusMeters : activeMaxRadiusMeters(project);
  QgsCoordinateReferenceSystem dest = project->crs();
  if (!dest.isValid() || dest.isGeographic()) dest = source->crs();
  if (!dest.isValid() || dest.isGeographic()) dest = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5187"));

  const QgsGeometry sitePoly = unionPolygonsInCrs(source, dest, project, &out.error);
  if (sitePoly.isEmpty()) return out;

  struct GroupedSite {
    Item item;
    QVector<QgsGeometry> parts;
  };
  for (QgsVectorLayer* layer : heritageLayers) {
    const auto ds = datasetForLayer(project, layer);
    if (!ds) continue;
    const QString datasetName = HeritageStyle::layerName(*ds);
    const Fields fields = resolveFields(layer, *ds);
    const bool needXf = layer->crs().isValid() && layer->crs() != dest;
    QgsCoordinateTransform xf(layer->crs(), dest, project->transformContext());
    if (needXf) xf.setBallparkTransformsAreAppropriate(true);
    QMap<QString, GroupedSite> grouped;
    QgsFeatureIterator it = layer->getFeatures();
    QgsFeature f;
    while (it.nextFeature(f)) {
      QgsGeometry g = f.geometry();
      if (g.isEmpty()) continue;
      if (needXf) {
        try {
          if (g.transform(xf) != Qgis::GeometryOperationResult::Success) continue;
        } catch (...) {
          KA_LOG_EXCEPT();
          continue;
        }
      }
      const QString code = attributeText(f, fields.code);
      // 이름이 아니라 유산코드로 묶는다. 같은 이름의 다른 유적(예: "조사구역내 유적")을 합치지 않는다.
      const QString key = code.isEmpty() ? QStringLiteral("__fid_%1").arg(f.id()) : QStringLiteral("code:%1").arg(code);
      auto& entry = grouped[key];
      if (entry.item.siteName.isEmpty()) {
        const QString label = attributeText(f, fields.label);
        entry.item = Item{
            datasetName, layer->name(), label.isEmpty() ? HeritageStyle::unnamedLabel() : label, {}, {}, 0, -1.0, {}};
        fillMetadata(&entry.item, f, fields);
      }
      // 표에서 같은 이름의 도형을 구분할 수 있도록 코드/피처ID 키를 남긴다.
      entry.item.featureKey = key;
      entry.parts.append(g);
    }
    for (auto git = grouped.cbegin(); git != grouped.cend(); ++git) {
      const QVector<QgsGeometry>& parts = git.value().parts;
      const QgsGeometry g = (parts.size() == 1) ? parts.first() : QgsGeometry::unaryUnion(parts);
      const double rawDist = g.isEmpty() ? -1.0 : sitePoly.distance(g);
      if (rawDist < 0.0 || !std::isfinite(rawDist) || rawDist > out.maxRadiusMeters) continue;

      Item item = git.value().item;
      if (sitePoly.intersects(g) || rawDist < 0.05) {
        item.direction = QStringLiteral("조사구역 내");
        item.radiusLabel = QStringLiteral("조사구역 내");
        item.description = QStringLiteral("조사구역 내 (0m)");
      } else {
        item.distanceMeters = std::max(1, qRound(rawDist));
        item.radiusLabel = (rawDist <= 500.0) ? QStringLiteral("500m 이내")
                                              : QStringLiteral("%1m 이내").arg(qRound(out.maxRadiusMeters));
        const double az = computeAzimuthDeg(sitePoly, g);
        item.azimuthDeg = std::round(az * 10.0) / 10.0;
        item.direction = cardinalDirection8(az);
        item.description = QStringLiteral("조사구역에서 %1쪽으로 약 %2m").arg(item.direction).arg(item.distanceMeters);
      }
      out.items.append(item);
    }
  }
  std::stable_sort(out.items.begin(), out.items.end(), [](const Item& a, const Item& b) {
    if (a.distanceMeters != b.distanceMeters) return a.distanceMeters < b.distanceMeters;
    return a.datasetName != b.datasetName ? a.datasetName < b.datasetName : a.siteName < b.siteName;
  });
  return out;
}
}  // namespace NearbyHeritageExport
