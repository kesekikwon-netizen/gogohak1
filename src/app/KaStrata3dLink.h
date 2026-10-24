#pragma once

#include <QObject>
#include <QPointer>

#include <functional>

#include "core/Strata3dLink.h"

class QLabel;
class QPushButton;
class QVBoxLayout;
class QgsProject;

// 입체지형 탭 왼쪽 패널에 strata-3d(정밀 3D·고고학 평가) 호출 단추 둘을 붙인다.
// 「strata-3d 로 열기」= 열려 있는 조사 파일을 넘기고 조사구역 평가 요약을 돌려받는다.
// 「strata-3d 도면 받기」= strata-3d 가 그린 입체지형 도면(PNG·PDF)을 받아 저장한다.
class KaStrata3dLink : public QObject {
  Q_OBJECT
public:
  KaStrata3dLink(QVBoxLayout* panelLayout, QLabel* status, QgsProject* project, QWidget* owner);

private:
  void openSurvey();
  void receiveSheet();
  void runAsync(const QString& what, std::function<Strata3dLink::Outcome()> job);  // 워커 스레드에서 돌리고 결과는 UI 스레드로
  void finished(const QString& what, const Strata3dLink::Outcome& r);
  void setBusy(bool busy);

  QPointer<QgsProject> m_project;
  QPointer<QWidget> m_owner;
  QLabel* m_status = nullptr;
  QPushButton* m_btnOpen = nullptr;
  QPushButton* m_btnSheet = nullptr;
};
