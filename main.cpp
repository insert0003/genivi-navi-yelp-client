#include <QApplication>
#include <iostream>
#include "MainApp.h"
#include <getopt.h>

#include <libhomescreen.hpp>
#include <qlibwindowmanager.h>


#define DEFAULT_CREDENTIALS_FILE "/etc/poikey"

using namespace std;

QLibWindowmanager* qwm;
LibHomeScreen* hs;
QString graphic_role;
MainApp *mainapp;

void SyncDrawHandler(json_object *object)
{
	qwm->endDraw(graphic_role);
}

void TapShortcutHandler(json_object *object)
{
    qwm->activateWindow(graphic_role);
}

void SetDestinationHandler(json_object *object)
{
    double latitude;
    double longitude;

    struct json_object *json_parameter;
    struct json_object *json_latitude;
    struct json_object *json_longitude;
    cerr << "SetDestinationHandler: get latitudeInDegrees object:" <<json_object_get_string(object)<< endl;
    if(!json_object_object_get_ex(object, "parameter", &json_parameter)) {
        cerr << "Error: get parameter failed" << endl;
        return;
    }

    if(!json_object_object_get_ex(json_parameter, "latitudeInDegrees", &json_latitude)) {
        cerr << "Error: get latitudeInDegrees failed" << endl;
        return;
    }
    latitude = json_object_get_double(json_latitude);

    if(!json_object_object_get_ex(json_parameter, "longitudeInDegrees", &json_longitude)) {
        cerr << "Error: get longitudeInDegrees failed" << endl;
        return;
    }
    longitude = json_object_get_double(json_longitude);

    cerr << "SetDestinationHandler: latitude:" <<latitude<< ",longitude:" <<longitude<< endl;

    mainapp->SetDestination(latitude, longitude);
}

int main(int argc, char *argv[], char *env[])
{
    int opt;
    QApplication a(argc, argv);
    QString credentialsFile(DEFAULT_CREDENTIALS_FILE);
    qwm = new QLibWindowmanager();
    hs = new LibHomeScreen();
	graphic_role = QString("poi");

	QString pt = QString(argv[1]);
	int port = pt.toInt();
	QString secret = QString(argv[2]);
	std::string token = secret.toStdString();

    if (qwm->init(port, secret) != 0) {
        exit(EXIT_FAILURE);
    }

    if (qwm->requestSurface(graphic_role) != 0) {
        cerr << "Error: wm check failed" << endl;
        exit(EXIT_FAILURE);
    }

	qwm->set_event_handler(QLibWindowmanager::Event_SyncDraw, SyncDrawHandler);

    mainapp = new MainApp();

    hs->init(port, token.c_str());

	hs->set_event_handler(LibHomeScreen::Event_TapShortcut, TapShortcutHandler);
    hs->set_event_handler(LibHomeScreen::Event_SetDestination, SetDestinationHandler);

    //force setting
    mainapp->setInfoScreen(true);
    mainapp->setKeyboard(true);

    /* check naviapi */
    if (mainapp->CheckNaviApi(argc, argv) == false)
    {
        cerr << "Error: naviapi check failed" << endl;
        return -1;
    }

    /* then, authenticate connexion to POI service: */
    if (mainapp->AuthenticatePOI(credentialsFile) < 0)
    {
        cerr << "Error: POI server authentication failed" << endl;
        return -1;
    }

    cerr << "authentication succes !" << endl;

    /* now, let's start monitor user inut (register callbacks): */
    if (mainapp->StartMonitoringUserInput() < 0)
        return -1;

	//qwm->activateWindow(graphic_role);
    hs->publishSubscription();

    /* main loop: */
    return a.exec();
}
