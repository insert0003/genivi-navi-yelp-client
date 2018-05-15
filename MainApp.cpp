#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkProxy>
#include <QTreeWidget>
#include <iostream>
#include <error.h>
#include <json-c/json.h>
#include <stdlib.h>
#include <unistd.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "MainApp.h"
#include "Business.h"
#include "InfoPanel.h"
#include "ClickableLabel.h"
#include "Keyboard.h"
#include "traces.h"

#define DEFAULT_TEXT        "Select your destination with Yelp !"
#define URL_AUTH            "https://api.yelp.com/oauth2/token"
#define URL_AUTOCOMPLETE    "https://api.yelp.com/v3/autocomplete"
#define URL_SEARCH          "https://api.yelp.com/v3/businesses/search"

#define BIG_BUFFER_SIZE     (1024*1024)
#define LEFT_OFFSET         28
#define FONT_SIZE_LINEDIT   20
#define FONT_SIZE_LIST      18
#define TEXT_INPUT_WIDTH    800
#define SEARCH_BTN_SIZE     105
#define SPACER              15
#define WIDGET_WIDTH        (SEARCH_BTN_SIZE + SPACER + TEXT_INPUT_WIDTH)
#define DISPLAY_WIDTH       TEXT_INPUT_WIDTH
#define DISPLAY_HEIGHT      480
#define COMPLETE_W_WITH_KB    1080
#define COMPLETE_H_WITH_KB    1487
#define RESULT_ITEM_HEIGHT  80
#define MARGINS             25
#define AGL_REFRESH_DELAY   75 /* milliseconds */

#define SCROLLBAR_STYLE \
"QScrollBar:vertical {" \
"    border: 2px solid grey;" \
"    background: gray;" \
"    width: 45px;" \
"}"

using namespace std;

MainApp::MainApp():QMainWindow(Q_NULLPTR, Qt::FramelessWindowHint),
    networkManager(this),searchBtn(QIcon(tr(":/images/loupe-90.png")), tr(""), this),
    lineEdit(this),keyboard(QRect(0, 688, COMPLETE_W_WITH_KB, 720), this),
    mutex(QMutex::Recursive),token(""),currentSearchingText(""),currentSearchedText(""),
    pSearchReply(NULL),pInfoPanel(NULL),pResultList(NULL),currentLatitude(0.0),currentLongitude(0.0),
    navicoreSession(0),currentIndex(0),fontId(-1),isInfoScreen(false),
    isInputDisplayed(false),isKeyboard(false),isAglNavi(false)
{
    //this->setAttribute(Qt::WA_TranslucentBackground);
    this->setStyleSheet("border: none;");

    searchBtn.setStyleSheet("border: none; color: #FFFFFF;");
    searchBtn.setMinimumSize(QSize(SEARCH_BTN_SIZE, SEARCH_BTN_SIZE));
    searchBtn.setIconSize(searchBtn.size());
    searchBtn.setGeometry(QRect(LEFT_OFFSET, 0, searchBtn.width(), searchBtn.height()));

    lineEdit.setStyleSheet("border: none; color: #FFFFFF;");
    lineEdit.setMinimumSize(QSize(TEXT_INPUT_WIDTH, SEARCH_BTN_SIZE));

    lineEdit.setPlaceholderText(QString(DEFAULT_TEXT));
    font = lineEdit.font();
    font.setPointSize(FONT_SIZE_LINEDIT);
    lineEdit.setFont(font);
    lineEdit.setTextMargins(MARGINS/2, 0, 0, 0);
    lineEdit.installEventFilter(this);
    lineEdit.setGeometry(QRect(LEFT_OFFSET + searchBtn.width() + SPACER, 0, lineEdit.width(), lineEdit.height()));
    lineEdit.setVisible(false);

    /* We might need a Japanese font: */
    QFile fontFile(":/fonts/DroidSansJapanese.ttf");
    if (!fontFile.open(QIODevice::ReadOnly))
    {
        TRACE_ERROR("failed to open font file");
    }
    else
    {
        QByteArray fontData = fontFile.readAll();
        fontId = QFontDatabase::addApplicationFontFromData(fontData);
        if (fontId < 0)
        {
            TRACE_ERROR("QFontDatabase::addApplicationFontFromData failed");
        }
    }
    
    /* Check if "AGL_NAVI" env variable is set. If yes, we must notify
     * AGL environment when surface needs to be resized */
    if (getenv("AGL_NAVI"))
        isAglNavi = true;

    connect(this, SIGNAL(allSessionsGotSignal()), this, SLOT(allSessionsGot()));
    connect(this, SIGNAL(positionGotSignal()), this, SLOT(positionGot()));
    connect(this, SIGNAL(allRoutesGotSignal()), this, SLOT(allRoutesGot()));
    connect(this, SIGNAL(routeCreatedSignal()), this, SLOT(routeCreated()));

    this->setGeometry(QRect(this->pos().x(), this->pos().y(), COMPLETE_W_WITH_KB, COMPLETE_H_WITH_KB));
    this->setStyleSheet("background-image: url(:/images/AGL_POI_Background.png);");
    this->show();
}

MainApp::~MainApp()
{
    mutex.lock();
    if (fontId >= 0)
        QFontDatabase::removeApplicationFont(fontId);

    searchBtn.disconnect();
    lineEdit.disconnect();
    networkManager.disconnect();
    keyboard.disconnect();
        
    delete pSearchReply;
    delete pInfoPanel;
    mutex.unlock();
}

void MainApp::searchBtnClicked()
{
    isInputDisplayed = !isInputDisplayed;
    TRACE_DEBUG("isInputDisplayed = %d", isInputDisplayed);
    DisplayLineEdit(isInputDisplayed);
}

void MainApp::DisplayLineEdit(bool display)
{
    mutex.lock();

    this->setGeometry(QRect(this->pos().x(), this->pos().y(), COMPLETE_W_WITH_KB, COMPLETE_H_WITH_KB));

    if (display)
    {
        lineEdit.setVisible(true);
        lineEdit.setFocus();
    }
    else
    {
        if (pResultList)
        {
            pResultList->removeEventFilter(this);
            delete pResultList;
            pResultList = NULL;
        }
        if (pInfoPanel)
        {
            delete pInfoPanel;
            pInfoPanel = NULL;
        }
        lineEdit.setText(tr(""));
        lineEdit.setVisible(false);
    }
    isInputDisplayed = display;
    
    mutex.unlock();
}

void MainApp::UpdateAglSurfaces()
{
    char cmd[1024];

    TRACE_DEBUG("handle AGL demo surfaces (new surface is bigger)");
    snprintf(cmd, 1023, "/usr/bin/LayerManagerControl set surface $SURFACE_ID_CLIENT source region 0 0 %d %d",
        this->width(), this->height());
    TRACE_DEBUG("%s", cmd);
    system(cmd);
    snprintf(cmd, 1023, "/usr/bin/LayerManagerControl set surface $SURFACE_ID_CLIENT destination region $CLIENT_X $CLIENT_Y %d %d",
        this->width(), this->height());
    TRACE_DEBUG("%s", cmd);
    system(cmd);
}

void MainApp::DisplayResultList(bool display, bool RefreshDisplay)
{
    mutex.lock();

    if (display)
    {
        if (!pResultList)
        {
            pResultList = new QTreeWidget(this);
            pResultList->setStyleSheet("border: none; color: #FFFFFF;");
            pResultList->setRootIsDecorated(false);
            pResultList->setEditTriggers(QTreeWidget::NoEditTriggers);
            pResultList->setSelectionBehavior(QTreeWidget::SelectRows);
            pResultList->setFrameStyle(QFrame::Box | QFrame::Plain);
            pResultList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            //pResultList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            pResultList->setAttribute(Qt::WA_AcceptTouchEvents);
            pResultList->verticalScrollBar()->setStyleSheet(SCROLLBAR_STYLE);
            pResultList->header()->hide();
            //font.setPointSize(FONT_SIZE_LIST);
            //pResultList->setFont(font);
            pResultList->installEventFilter(this);
        }
        
        pResultList->setGeometry(QRect(   LEFT_OFFSET+searchBtn.width()+SPACER, searchBtn.height()+SPACER,
                                        DISPLAY_WIDTH, DISPLAY_HEIGHT));
        if (RefreshDisplay)
        {
            this->setGeometry(QRect(this->pos().x(), this->pos().y(), COMPLETE_W_WITH_KB, COMPLETE_H_WITH_KB));
        }
        pResultList->setVisible(true);
        pResultList->setFocus();
    }
    else
    {
        if (pResultList)
        {
            pResultList->removeEventFilter(this);
            pResultList->deleteLater();
            pResultList = NULL;
        }

        lineEdit.setFocus();
        
        if (RefreshDisplay)
        {
            this->setGeometry(QRect(this->pos().x(), this->pos().y(), COMPLETE_W_WITH_KB, COMPLETE_H_WITH_KB));
        }
    }
    
    mutex.unlock();
}

void MainApp::textChanged(const QString & text)
{
    TRACE_INFO("New text is: %s", qPrintable(text));

    /* do not handle text input if info panel is displayed: */
    if (pInfoPanel) return;

    mutex.lock();

    delete pSearchReply;    /* cancel current search */
    pSearchReply = NULL;

    if (text.length() == 0) /* if empty text -> no search */
    {
        DisplayResultList(false);
        mutex.unlock();
        return;
    }

    /* if text is the same as previous search -> no need to search again */
    if (text == currentSearchedText)
    {
        DisplayResultList(true);
        FillResultList(Businesses, currentIndex);
        mutex.unlock();
        return;
    }
    this->currentSearchingText = text;

    /* we need to know our current position */
    std::vector<int32_t> Params;
    Params.push_back(naviapi::NAVICORE_LONGITUDE);
    Params.push_back(naviapi::NAVICORE_LATITUDE);
    naviapi.getPosition(Params);

    mutex.unlock();
}

void MainApp::textAdded(const QString & text)
{
    mutex.lock();
    lineEdit.setText(lineEdit.text() + text);
    mutex.unlock();
}

void MainApp::keyPressed(int key)
{
    mutex.lock();
    if (key == '\b') /* backspace */
    {
        int len = lineEdit.text().length();
        if (len > 0)
            lineEdit.setText(lineEdit.text().remove(len-1, 1));
    }
    mutex.unlock();
}

void MainApp::itemClicked()
{
    mutex.lock();
    if (isInfoScreen)
    {
        DisplayInformation(true, false);
    }
    else
    {
        SetDestination();
        DisplayLineEdit(false);
    }
    mutex.unlock();
}

void MainApp::ParseJsonBusinessList(const char* buf, std::vector<Business> & Output)
{
    json_object *jobj = json_tokener_parse(buf);
    if (!jobj)
    {
        TRACE_ERROR("json_tokener_parse failed");
        cerr << "json_tokener_parse failed: " << buf << endl;
        return;
    }

    json_object_object_foreach(jobj, key, val)
    {
        (void)key;
        json_object *value;
        
        if (json_object_get_type(val) == json_type_array)
        {
            TRACE_DEBUG_JSON("an array was found");

            if(json_object_object_get_ex(jobj, "businesses", &value))
            {
                TRACE_DEBUG_JSON("an business was found");

                int arraylen = json_object_array_length(value);

                for (int i = 0; i < arraylen; i++)
                {
                    Business NewBusiness;

                    json_object* medi_array_obj, *medi_array_obj_elem;
                    medi_array_obj = json_object_array_get_idx(value, i);
                    if (medi_array_obj)
                    {
                        if (json_object_object_get_ex(medi_array_obj, "rating", &medi_array_obj_elem))
                        {
                            NewBusiness.Rating = json_object_get_double(medi_array_obj_elem);
                            TRACE_DEBUG_JSON("got Rating : %f", NewBusiness.Rating);
                        }

                        if (json_object_object_get_ex(medi_array_obj, "distance", &medi_array_obj_elem))
                        {
                            NewBusiness.Distance = json_object_get_double(medi_array_obj_elem);
                            TRACE_DEBUG_JSON("got Distance : %f", NewBusiness.Distance);
                        }

                        if (json_object_object_get_ex(medi_array_obj, "review_count", &medi_array_obj_elem))
                        {
                            NewBusiness.ReviewCount = json_object_get_int(medi_array_obj_elem);
                            TRACE_DEBUG_JSON("got ReviewCount : %u", NewBusiness.ReviewCount);
                        }

                        if (json_object_object_get_ex(medi_array_obj, "name", &medi_array_obj_elem))
                        {
                            NewBusiness.Name = QString(json_object_get_string(medi_array_obj_elem));
                            TRACE_DEBUG_JSON("got Name : %s", qPrintable(NewBusiness.Name));
                        }

                        if (json_object_object_get_ex(medi_array_obj, "image_url", &medi_array_obj_elem))
                        {
                            NewBusiness.ImageUrl = QString(json_object_get_string(medi_array_obj_elem));
                            TRACE_DEBUG_JSON("got ImageUrl : %s", qPrintable(NewBusiness.ImageUrl));
                        }

                        if (json_object_object_get_ex(medi_array_obj, "phone", &medi_array_obj_elem))
                        {
                            NewBusiness.Phone = QString(json_object_get_string(medi_array_obj_elem));
                            TRACE_DEBUG_JSON("got Phone : %s", qPrintable(NewBusiness.Phone));
                        }

                        if (json_object_object_get_ex(medi_array_obj, "coordinates", &medi_array_obj_elem))
                        {
                            json_object *value2;
                            
                            TRACE_DEBUG_JSON("coordinates were found");

                            if(json_object_object_get_ex(medi_array_obj_elem, "latitude", &value2))
                            {
                                NewBusiness.Latitude = json_object_get_double(value2);
                                TRACE_DEBUG_JSON("got Latitude : %f", NewBusiness.Latitude);
                            }

                            if(json_object_object_get_ex(medi_array_obj_elem, "longitude", &value2))
                            {
                                NewBusiness.Longitude = json_object_get_double(value2);
                                TRACE_DEBUG_JSON("got Longitude : %f", NewBusiness.Longitude);
                            }
                        }

                        if (json_object_object_get_ex(medi_array_obj, "location", &medi_array_obj_elem))
                        {
                            json_object *value2;
                            
                            TRACE_DEBUG_JSON("a location was found");

                            /* TODO: how do we deal with address2 and address3 ? */
                            if(json_object_object_get_ex(medi_array_obj_elem, "address1", &value2))
                            {
                                NewBusiness.Address = QString(json_object_get_string(value2));
                                TRACE_DEBUG_JSON("got Address : %s", qPrintable(NewBusiness.Address));
                            }

                            if(json_object_object_get_ex(medi_array_obj_elem, "city", &value2))
                            {
                                NewBusiness.City = QString(json_object_get_string(value2));
                                TRACE_DEBUG_JSON("got City : %s", qPrintable(NewBusiness.City));
                            }

                            if(json_object_object_get_ex(medi_array_obj_elem, "state", &value2))
                            {
                                NewBusiness.State = QString(json_object_get_string(value2));
                                TRACE_DEBUG_JSON("got State : %s", qPrintable(NewBusiness.State));
                            }

                            if(json_object_object_get_ex(medi_array_obj_elem, "zip_code", &value2))
                            {
                                NewBusiness.ZipCode = QString(json_object_get_string(value2));
                                TRACE_DEBUG_JSON("got ZipCode : %s", qPrintable(NewBusiness.ZipCode));
                            }

                            if(json_object_object_get_ex(medi_array_obj_elem, "country", &value2))
                            {
                                NewBusiness.Country = QString(json_object_get_string(value2));
                                TRACE_DEBUG_JSON("got Country : %s", qPrintable(NewBusiness.Country));
                            }
                        }

                        /* TODO: parse categories */

                        /* Add business in our list: */
                        Businesses.push_back(NewBusiness);
                    }
                }
            }
        }
    }

    json_object_put(jobj);
}

bool MainApp::eventFilter(QObject *obj, QEvent *ev)
{
    bool ret = false;

    mutex.lock();

    if (obj == pResultList)
    {
        //TRACE_DEBUG("ev->type() = %d", (int)ev->type());

        if (ev->type() == QEvent::KeyPress)
        {
            bool consumed = false;
            int key = static_cast<QKeyEvent*>(ev)->key();
            TRACE_DEBUG("key pressed (%d)", key);
            switch (key) {
                case Qt::Key_Enter:
                case Qt::Key_Return:
                    TRACE_DEBUG("enter or return");
                    if (isInfoScreen)
                    {
                        DisplayInformation(true);
                    }
                    else
                    {
                        SetDestination();
                        DisplayLineEdit(false);
                    }
                    consumed = true;
                    break;

                case Qt::Key_Escape:
                    TRACE_DEBUG("escape");
                    DisplayResultList(false);
                    consumed = true;
                    break;

                case Qt::Key_Up:
                case Qt::Key_Down:
                case Qt::Key_Home:
                case Qt::Key_End:
                case Qt::Key_PageUp:
                case Qt::Key_PageDown:
                    TRACE_DEBUG("arrows");
                    break;

                default:
                    TRACE_DEBUG("default");
                    lineEdit.event(ev);
                    break;
            }

            mutex.unlock();
            return consumed;
        }
    }
    else if (obj == &lineEdit)
    {
        if (pInfoPanel && ev->type() == QEvent::KeyPress)
        {
            switch(static_cast<QKeyEvent*>(ev)->key())
            {
                case Qt::Key_Escape:
                    TRACE_DEBUG("Escape !");
                    DisplayInformation(false, false);
                    DisplayResultList(true);
                    FillResultList(Businesses, currentIndex);
                    break;
                case Qt::Key_Enter:
                case Qt::Key_Return:
                    TRACE_DEBUG("Go !");
                    SetDestination(currentIndex);
                    DisplayLineEdit(false);
                    break;
                default: break;
            }
        }
    }
    else
    {
        ret = QMainWindow::eventFilter(obj, ev);
    }
    mutex.unlock();
    return ret;
}

void MainApp::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (isAglNavi)
    {
        QTimer::singleShot(AGL_REFRESH_DELAY, Qt::CoarseTimer, this, SLOT(UpdateAglSurfaces()));
    }
}

void MainApp::SetDestination(int index)
{
    mutex.lock();

    /* if pResultList exists, take the selected index
     * otherwise, take the index given as parameter */
    if (pResultList)
    {
        QList<QTreeWidgetItem *> SelectedItems = pResultList->selectedItems();
        if (SelectedItems.size() > 0)
        {
            /* select the first selected item : */
            index = pResultList->indexOfTopLevelItem(*SelectedItems.begin());
        }
    }

    TRACE_DEBUG("index is: %d", index);

    /* retrieve the coordinates of this item : */
    this->destinationLatitude = Businesses[index].Latitude;
    this->destinationLongitude = Businesses[index].Longitude;

    naviapi.getAllRoutes();

    mutex.unlock();
}

void MainApp::DisplayInformation(bool display, bool RefreshDisplay)
{
    mutex.lock();
    if (display)
    {
        /* pResultList must exist, so that we can retrieve the selected index: */
        if (!pResultList)
        {
            TRACE_ERROR("pResultList is null");
            mutex.unlock();
            return;
        }

        QList<QTreeWidgetItem *> SelectedItems = pResultList->selectedItems();
        if (SelectedItems.size() <= 0)
        {
            TRACE_ERROR("no item is selected");
            mutex.unlock();
            return;
        }

        /* select the first selected item : */
        currentIndex = pResultList->indexOfTopLevelItem(*SelectedItems.begin());

        /* Resize window: */
        DisplayResultList(false, false);

        /* Display info for the selected item: */
        QRect rect( LEFT_OFFSET+searchBtn.width()+SPACER, searchBtn.height()+SPACER,
                    DISPLAY_WIDTH, DISPLAY_HEIGHT);
        pInfoPanel = new InfoPanel(this, Businesses[currentIndex], rect);

        if (RefreshDisplay)
        {
            this->setGeometry(QRect(this->pos().x(), this->pos().y(), COMPLETE_W_WITH_KB, COMPLETE_H_WITH_KB));
        }

        connect(pInfoPanel->getGoButton(),      SIGNAL(clicked(bool)), this, SLOT(goClicked()));
        connect(pInfoPanel->getCancelButton(),  SIGNAL(clicked(bool)), this, SLOT(cancelClicked()));
    }
    else
    {
        if (pInfoPanel)
        {
            pInfoPanel->getGoButton()->disconnect();
            pInfoPanel->getCancelButton()->disconnect();
            delete pInfoPanel;
            pInfoPanel = NULL;
        }
        lineEdit.setFocus();
        
        if (RefreshDisplay)
        {
            this->setGeometry(QRect(this->pos().x(), this->pos().y(), COMPLETE_W_WITH_KB, COMPLETE_H_WITH_KB));
        }
    }

    mutex.unlock();
}

void MainApp::networkReplySearch(QNetworkReply* reply)
{
    char buf[BIG_BUFFER_SIZE];
    int buflen;
    
    mutex.lock();

    /* memorize the text which gave this result: */
    currentSearchedText = lineEdit.text();

	if (reply->error() == QNetworkReply::NoError)
	{
	    // we only handle this callback if it matches the last search request:
	    if (reply != pSearchReply)
	    {
	        TRACE_INFO("this reply is already too late (or about a different network request)");
	        mutex.unlock();
	        return;
	    }
	    
    	buflen = reply->read(buf, BIG_BUFFER_SIZE-1);
	    buf[buflen] = '\0';
	
	    if (buflen == 0)
	    {
	        mutex.unlock();
	        return;
	    }
	
	
	
	    currentIndex = 0;
	    Businesses.clear();
	    ParseJsonBusinessList(buf, Businesses);
	    DisplayResultList(true);
	    FillResultList(Businesses);
    }
    else
    {
    	fprintf(stderr,"POI: reply error network please check to poikey and system time (adjusted?)\n");
    }
    
    mutex.unlock();
}

/* pResultList must be allocated at this point ! */
int MainApp::FillResultList(vector<Business> & list, int focusIndex)
{
    int nbElem = 0;

    mutex.lock();

    pResultList->setUpdatesEnabled(false);
    pResultList->clear();

    /* filling the dropdown menu: */
    for (vector<Business>::iterator it = list.begin(); it != list.end(); it++)
    {
        /*  workaround to avoid entries with wrong coordinates returned by Yelp: */
        if (IsCoordinatesConsistent(*it) == false)
        {
            list.erase(it--);
            continue;
        }

        QTreeWidgetItem * item = new QTreeWidgetItem(pResultList);

        ClickableLabel *label = new ClickableLabel("<b>"+(*it).Name+
            "</b><br>"+(*it).Address+", "+(*it).City+", "+(*it).State+
            " "+(*it).ZipCode+", "+(*it).Country, pResultList);
        label->setTextFormat(Qt::RichText);
        font.setPointSize(FONT_SIZE_LIST);
        label->setFont(font);
        label->setIndent(MARGINS);
        label->setAttribute(Qt::WA_AcceptTouchEvents);
        item->setSizeHint(0, QSize(TEXT_INPUT_WIDTH, RESULT_ITEM_HEIGHT));
        pResultList->setItemWidget(item, 0, label);
        connect(label, SIGNAL(clicked()), this, SLOT(itemClicked()));

        //item->setText(0, (*it).Name);

        if (nbElem == focusIndex)
        {
            pResultList->setCurrentItem(item);
        }
        nbElem++;
    }

    pResultList->setUpdatesEnabled(true);

    mutex.unlock();
    return nbElem;
}

/* Well... some of the POI returned by Yelp have coordinates which are
 * completely inconsistent with the distance at which the POI is
 * supposed to be.
 * https://github.com/Yelp/yelp-fusion/issues/104
 * Let's skip them for the moment: */
#define PI 3.14159265
#define EARTH_RADIUS 6371000
static inline double toRadians(double a) { return a * PI / 180.0; }
bool MainApp::IsCoordinatesConsistent(Business & business)
{
    double lat1 = toRadians(currentLatitude);
    double lon1 = toRadians(currentLongitude);
    double lat2 = toRadians(business.Latitude);
    double lon2 = toRadians(business.Longitude);
    double x = (lon2 - lon1) * cos((lat1 + lat2)/2);
    double y = lat2 - lat1;
    double DistanceFromCoords = EARTH_RADIUS * sqrt(pow(x, 2) + pow(y, 2));

    /* if calculated distance is not between +/- 10% of the announced
     * distance -> skip this POI: */
    if (DistanceFromCoords < business.Distance * 0.9 ||
        DistanceFromCoords > business.Distance * 1.1)
    {
        TRACE_ERROR("Announced distance: %f, calculated distance: %f", business.Distance, DistanceFromCoords);
        return false;
    }

    return true;
}
/* end of workaround */

bool MainApp::CheckNaviApi(int argc, char *argv[])
{
    bool ret = naviapi.connect(argc, argv, this);

    if (ret == true)
    {
        naviapi.getAllSessions();
    }

    return ret;
}

int MainApp::AuthenticatePOI(const QString & CredentialsFile)
{
    char buf[512];
    QString AppId;
    QString AppSecret;
    QString ProxyHostName;
    QString PortStr;
    QString User;
    QString Password;
    int portnum;

    /* First, read AppId and AppSecret from credentials file: */
    FILE* filep = fopen(qPrintable(CredentialsFile), "r");
    if (!filep)
    {
        fprintf(stderr,"Failed to open credentials file \"%s\": %m", qPrintable(CredentialsFile));
        return -1;
    }

    if (!fgets(buf, 512, filep))
    {
        fprintf(stderr,"Failed to read AppId from credentials file \"%s\"", qPrintable(CredentialsFile));
        fclose(filep);
        return -1;
    }
    if (strlen(buf) > 0 && buf[strlen(buf)-1] == '\n')
        buf[strlen(buf)-1] = '\0';
    AppId = QString(buf);
    
    if (!fgets(buf, 512, filep))
    {
        fprintf(stderr,"Failed to read AppSecret from credentials file \"%s\"", qPrintable(CredentialsFile));
        fclose(filep);
        return -1;
    }
    if (strlen(buf) > 0 && buf[strlen(buf)-1] == '\n')
        buf[strlen(buf)-1] = '\0';
    AppSecret = QString(buf);

    QNetworkProxy proxy;

    //ProxyHostName
    if (!fgets(buf, 512, filep))
    {
        TRACE_INFO("Failed to read ProxyHostName from credentials file \"%s\"", qPrintable(CredentialsFile));
    }
    else
    {
        if (strlen(buf) > 0 && buf[strlen(buf)-1] == '\n')
            buf[strlen(buf)-1] = '\0';
        ProxyHostName = QString(buf);
        ProxyHostName.replace(0, 14, tr(""));

        //Port
        if (!fgets(buf, 512, filep))
        {
            TRACE_ERROR("Failed to read Port from credentials file \"%s\"", qPrintable(CredentialsFile));
            fclose(filep);
            return -1;
        }
        if (strlen(buf) > 0 && buf[strlen(buf)-1] == '\n')
            buf[strlen(buf)-1] = '\0';
        PortStr = QString(buf);
        PortStr.replace(0, 5, tr(""));
        portnum = PortStr.toInt();

        //User
        if (!fgets(buf, 512, filep))
        {
            TRACE_ERROR("Failed to read User from credentials file \"%s\"", qPrintable(CredentialsFile));
            fclose(filep);
            return -1;
        }
        if (strlen(buf) > 0 && buf[strlen(buf)-1] == '\n')
            buf[strlen(buf)-1] = '\0';
        User = QString(buf);
        User.replace(0, 5, tr(""));

        //Password
        if (!fgets(buf, 512, filep))
        {
            TRACE_ERROR("Failed to read Password from credentials file \"%s\"", qPrintable(CredentialsFile));
            fclose(filep);
            return -1;
        }
        if (strlen(buf) > 0 && buf[strlen(buf)-1] == '\n')
            buf[strlen(buf)-1] = '\0';
        Password = QString(buf);
        Password.replace(0, 9, tr(""));

        proxy.setType(QNetworkProxy::HttpProxy);
        proxy.setHostName(qPrintable(ProxyHostName));
        proxy.setPort(portnum);
        proxy.setUser(qPrintable(User));
        proxy.setPassword(qPrintable(Password));
        QNetworkProxy::setApplicationProxy(proxy);
    }

    fclose(filep);

    TRACE_INFO("Found credentials");

    /* Then, send a HTTP request to get the token and wait for answer (synchronously): */

	token = AppSecret;
	return 0;
}

int MainApp::StartMonitoringUserInput()
{
    connect(&searchBtn, SIGNAL(clicked(bool)), this, SLOT(searchBtnClicked()));
    connect(&lineEdit, SIGNAL(textChanged(const QString &)), this, SLOT(textChanged(const QString &)));
    connect(&networkManager, SIGNAL(finished(QNetworkReply*)), this, SLOT(networkReplySearch(QNetworkReply*)));
    connect(&keyboard, SIGNAL(keyClicked(const QString &)), this, SLOT(textAdded(const QString &)));
    connect(&keyboard, SIGNAL(specialKeyClicked(int)), this, SLOT(keyPressed(int)));
    return 1;
}

void MainApp::SetWayPoints(uint32_t myRoute)
{
    /* set the destination : */
    naviapi::Waypoint destWp(this->destinationLatitude, this->destinationLongitude);
    std::vector<naviapi::Waypoint> myWayPoints;
    myWayPoints.push_back(destWp);
    naviapi.setWaypoints(navicoreSession, myRoute, true, myWayPoints);

    naviapi.calculateRoute(navicoreSession, myRoute);

    /* reset search: */
    currentSearchingText = tr("");
    currentSearchedText = tr("");
    currentIndex = 0;
    Businesses.clear();
}

void MainApp::goClicked()
{
    TRACE_DEBUG("Go clicked !");
    SetDestination(currentIndex);
    DisplayLineEdit(false);
}

void MainApp::cancelClicked()
{
    TRACE_DEBUG("Cancel clicked !");
    DisplayInformation(false, false);
    DisplayResultList(true, false);
    FillResultList(Businesses, currentIndex);
}

void MainApp::getAllSessions_reply(const std::map< uint32_t, std::string >& allSessions)
{
    mutex.lock();

    if (allSessions.empty())
    {
        TRACE_ERROR("Error: could not find an instance of Navicore");
        mutex.unlock();
        return;
    }

    this->navicoreSession = allSessions.begin()->first;

    TRACE_INFO("Current session: %d", this->navicoreSession);

    mutex.unlock();

    emit allSessionsGotSignal();
}


void MainApp::getPosition_reply(std::map< int32_t, naviapi::variant > position)
{
    mutex.lock();

    std::map< int32_t, naviapi::variant >::iterator it;
    for (it = position.begin(); it != position.end(); it++)
    {
        if (it->first == naviapi::NAVICORE_LATITUDE)
        {
            currentLatitude = it->second._double;
        }
        else if (it->first == naviapi::NAVICORE_LONGITUDE)
        {
            currentLongitude = it->second._double;
        }
    }

    TRACE_INFO("Current position: %f, %f", currentLatitude, currentLongitude);

    mutex.unlock();

    emit positionGotSignal();
}

void MainApp::getAllRoutes_reply(std::vector< uint32_t > allRoutes)
{
    mutex.lock();

    uint32_t routeHandle = 0;

    if (allRoutes.size() != 0)
    {
        routeHandle = allRoutes[0];
    }

    this->currentRouteHandle = routeHandle;

    mutex.unlock();

    emit allRoutesGotSignal();
}

void MainApp::createRoute_reply(uint32_t routeHandle)
{
    mutex.lock();

    this->currentRouteHandle = routeHandle;

    mutex.unlock();

    emit routeCreatedSignal();
}

void MainApp::allSessionsGot()
{
    mutex.lock();

    // nothing to do

    mutex.unlock();
}

void MainApp::positionGot()
{
    mutex.lock();

    /* let's generate a search request : */
    QString myUrlStr = URL_SEARCH + tr("?") + tr("term=") + currentSearchingText +
        tr("&latitude=") + QString::number(currentLatitude) +
        tr("&longitude=") + QString::number(currentLongitude);

    TRACE_DEBUG("URL: %s", qPrintable(myUrlStr));

    QUrl myUrl = QUrl(myUrlStr);
    QNetworkRequest req(myUrl);
    req.setRawHeader(QByteArray("Authorization"), (tr("bearer ") + token).toLocal8Bit());

    /* Then, send a HTTP request to get the token and wait for answer (synchronously): */

    pSearchReply = networkManager.get(req);

    mutex.unlock();
}

void MainApp::allRoutesGot()
{
    mutex.lock();

    /* check if a route already exists, if not create it : */
    if (this->currentRouteHandle == 0)
    {
        naviapi.createRoute(navicoreSession);
    }
    else
    {
        naviapi.pauseSimulation(navicoreSession);
        naviapi.setSimulationMode(navicoreSession, false);
        naviapi.cancelRouteCalculation(navicoreSession, this->currentRouteHandle);
        sleep(1);

        SetWayPoints(this->currentRouteHandle);
    }

    mutex.unlock();
}

void MainApp::routeCreated()
{
    mutex.lock();

    SetWayPoints(this->currentRouteHandle);

    mutex.unlock();
}

