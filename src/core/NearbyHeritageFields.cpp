#include "NearbyHeritageFields.h"
#include "HeritageImport.h"

#include <QMetaType>
#include <qgsfeature.h>
#include <qgsfields.h>
#include <qgsvectorlayer.h>

namespace NearbyHeritageExport {
namespace {

bool isText(const QgsVectorLayer* layer, int index) {
  return layer && index >= 0 && index < layer->fields().count() &&
         static_cast<QMetaType::Type>(layer->fields().at(index).type()) == QMetaType::QString;
}

QString findField(const QgsVectorLayer* layer, std::initializer_list<const char*> names) {
  if (!layer || !layer->isValid()) return {};
  const QgsFields fields = layer->fields();
  for (const char* name : names) {
    const QString want = QString::fromUtf8(name);
    const int i = fields.indexOf(want);
    if (i >= 0 && isText(layer, i)) return fields.at(i).name();
  }
  for (const char* name : names) {
    const QString want = QString::fromUtf8(name).toLower();
    for (int i = 0; i < fields.count(); ++i)
      if (fields.at(i).name().trimmed().toLower() == want && isText(layer, i)) return fields.at(i).name();
  }
  return {};
}

QString labelCandidates(HeritageDataset dataset, const QgsVectorLayer* layer) {
  switch (dataset) {
    case HeritageDataset::ExcavationSurveyArea:
    case HeritageDataset::SurfaceSurveyArea:
      return findField(layer, {"사업명", "국가유산명", "명칭", "유적명"});
    case HeritageDataset::AlterationStandard:
      return findField(layer, {"지정종목", "국가유산명", "명칭", "유적명"});
    case HeritageDataset::DesignatedHeritage:
      return findField(layer, {"국가유산명", "명칭", "유적명"});
    case HeritageDataset::BuriedHeritageArea:
      return findField(layer, {"유적명", "명칭", "국가유산명"});
    case HeritageDataset::HeritageDistributionMap:
      return findField(layer, {"명칭", "유적명", "국가유산명"});
  }
  return {};
}

}  // namespace

Fields resolveFields(const QgsVectorLayer* layer, HeritageDataset dataset) {
  Fields out;
  out.label = labelCandidates(dataset, layer);
  if (out.label.isEmpty()) out.label = HeritageImport::chooseNameField(layer);
  out.code = findField(layer, {"유산코드"});
  out.project = findField(layer, {"사업명"});
  out.report = findField(layer, {"보고서명"});
  out.address = findField(layer, {"소재지", "유적소재지"});
  out.institution = findField(layer, {"조사기관"});
  out.era = findField(layer, {"시대"});
  out.presence = findField(layer, {"유적유무"});
  out.permit = findField(layer, {"허가번호", "조사허가번호"});
  out.feature = findField(layer, {"유적명", "명칭"});
  return out;
}

QString attributeText(const QgsFeature& feature, const QString& fieldName) {
  if (fieldName.isEmpty() || !feature.isValid()) return {};
  const int index = feature.fieldNameIndex(fieldName);
  if (index < 0 || !feature.attribute(index).isValid()) return {};
  return feature.attribute(index).toString().trimmed();
}

}  // namespace NearbyHeritageExport
