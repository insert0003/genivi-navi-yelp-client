#ifndef __MAINAPP_H__
#define __MAINAPP_H__

#include <QMainWindow>
#include <QtWidgets>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMutex>
#include <QTreeWidget>
#include <vector>
#include "Business.h"
#include "InfoPanel.h"
#include "Keyboard.h"

#include <libnavicore.hpp>

class MainApp: public QMainWindow, public naviapi::NavicoreListener
{
    Q_OBJECT

    public:
        explicit MainApp();
        ~MainApp();
        bool CheckNaviApi(int argc, char *argv[]);
        int AuthenticatePOI(const QString & CredentialsFile);
        int StartMonitoringUserInput();
        void setInfoScreen(bool val) { isInfoScreen = val; }
        void setKeyboard(bool val)   { isKeyboard = val; }

    private:
        void ParseJsonBusinessList(const char* buf, std::vector<Business> & Output);
        bool eventFilter(QObject *obj, QEvent *ev);
        void resizeEvent(QResizeEvent* event);
        void SetDestination(int index = 0);
        bool IsCoordinatesConsistent(Business & business);
        void DisplayLineEdit(bool display = true);
        void DisplayResultList(bool display, bool RefreshDisplay = true);
        void DisplayInformation(bool display, bool RefreshDisplay = true);
        int FillResultList(std::vector<Business> & list, int focusIndex = 0);
        void SetWayPoints(uint32_t myRoute);

	naviapi::Navicore naviapi;
        QNetworkAccessManager networkManager;
        QPushButton searchBtn;
        QLineEdit lineEdit;
        Keyboard keyboard;
        QMutex mutex; // to protect pointers from concurrent access
        QString token;
        QString currentSearchingText;
        QString currentSearchedText;
        QNetworkReply *pSearchReply;
        InfoPanel *pInfoPanel;
        QTreeWidget *pResultList;
        double currentLatitude;
        double currentLongitude;
        double destinationLatitude;
        double destinationLongitude;
        uint32_t navicoreSession;
        uint32_t currentRouteHandle;
        int currentIndex;
        int fontId;
        bool isInfoScreen;
        bool isInputDisplayed;
        bool isKeyboard;
        bool isAglNavi;
        std::vector<Business> Businesses;
        QFont font;

    public:
        void getAllSessions_reply(const std::map< uint32_t, std::string >& allSessions);
	void getPosition_reply(std::map< int32_t, naviapi::variant > position);
	void getAllRoutes_reply(std::vector< uint32_t > allRoutes);
	void createRoute_reply(uint32_t routeHandle);

    private slots:
        void searchBtnClicked();
        void textChanged(const QString & text);
        void textAdded(const QString & text);
        void keyPressed(int key);
        void itemClicked();
        void networkReplySearch(QNetworkReply* reply);
        void UpdateAglSurfaces();
        void goClicked();
        void cancelClicked();

        void allSessionsGot();
        void positionGot();
        void allRoutesGot();
        void routeCreated();

    signals:
        void allSessionsGotSignal();
        void positionGotSignal();
        void allRoutesGotSignal();
        void routeCreatedSignal();
};

#endif // __MAINAPP_H__
