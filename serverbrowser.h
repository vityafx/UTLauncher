#include <QMainWindow>
#include <QTableView>
#include <QStringList>
#include <QJsonObject>
#include <QHeaderView>
#include <QList>
#include <QJsonArray>
#include <QStatusBar>

#include <QThread>
#include <QTcpSocket>
#include <QAbstractSocket>
#include <QMenu>
#include <QToolBar>
#include <QTableWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QProxyStyle>


#include <QDockWidget>

#include "awesome.h"

enum class Column {
    Country = 0,
    PlayerCount,
    GameMode,
    Map,
    Name,
    Ping,
    MaxColumn
};

#define MAX_PING 1999

class ServerEntry : public QObject {
    Q_OBJECT
    public:
    int id;
    QString name;
    QString host;
    int port;
    QString map;
    QString gameMode;
    QString countryCode;
    
    int maxPlayerCount = 0;
    int queryPort = 8890;
    int playerCount = 0;
    int ping = MAX_PING;
    
    float avgPing = MAX_PING;
    
    QTcpSocket* socket = nullptr;
    QByteArray lastQuery;
    QList<QByteArray> toQuery;
    QTime timer;
    QTime elapsed;
    QList<int> pingResults;
    
    QTimer queryTimer;
    QTime lastQueryTime;
    
    
    struct Player {
        QString name;
        int score;
        bool operator<(const Player &other) const {
            return other.score < score;
    }
    };
    QList<Player> players;
public:

    
    QString address() const {
        return host + ":" + QString::number(port);
    }
    
private slots:
    void onError(QAbstractSocket::SocketError socketError) {
        queryTimer.stop();
        queryTimer.singleShot(5000, this, SLOT(query()));
        ping = MAX_PING;
        avgPing = MAX_PING;
    }
public slots:
    void query() {
        if(socket) {
            socket->deleteLater();
        }
        socket = new QTcpSocket(this);
        qDebug() << host << queryPort;
        socket->connectToHost(host, queryPort);
            connect(socket, &QTcpSocket::stateChanged, [=](QAbstractSocket::SocketState state) {
                toQuery = QList<QByteArray>() << "GameMode" << "Map" << "PlayerNum" << "PlayerList";
            });
            
            connect(socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(onError(QAbstractSocket::SocketError)));
            
            connect(socket, &QTcpSocket::connected, [=]() {
                timer = QTime();
                timer.start();
                socket->write(toQuery.first());
            });
            connect(socket, &QTcpSocket::readyRead, [=]() {
                QString query = toQuery.takeFirst();
                QString answer = QString(socket->readAll()).trimmed();
                qDebug() << "Got answer" << answer;
                if(query == "Map") {
                    map = answer;
                } else if(query == "GameMode") {
                    gameMode = answer;
                } else if(query == "PlayerNum") {
                    playerCount = answer.toInt();
                } else if(query == "PlayerList") {
                    players.clear();
                    for(auto playerString : answer.split("\n")) {
                        QRegExp rx("(.*) S:(-?\\d+)");
                        if(rx.indexIn(playerString) != -1) {
                            Player p = {rx.cap(1), rx.cap(2).toInt()};
                            players.append(p);
                        }
                    }
                }
                
                pingResults.append(timer.elapsed());
                
                if(toQuery.size()) {
                    timer.start();
                    socket->write(toQuery.first());
                } else {
                    socket->close();
                    pingResults.removeFirst();
                    int sum = 0;
                    for(auto p: pingResults) {
                        sum += p;
                    }
                    ping = sum /= pingResults.size();
                    
                    if(avgPing > MAX_PING - 10) {
                        avgPing = ping;
                    } else {
                        avgPing = (9*avgPing + ping)/10;
                    }
                    qDebug() << "Ping" << ping;
                    
                    lastQueryTime = QTime::currentTime();
                    emit queryDone(id);
                    queryTimer.singleShot(10000 + qrand() % 10000, this, SLOT(query()));
                }
            });
    }

    void updateFromJson(const QJsonObject& object) {
        auto address = object.value("address").toString();
        
        QRegExp rx("(.*):(\\d+)");
        int pos = rx.indexIn(address);
        host = rx.cap(1);
        port = rx.cap(2).toInt();
        
        name = object.value("name").toString();
        countryCode = object.value("countryCode").toString();
        maxPlayerCount = object.value("maxPlayerCount").toInt();
        queryPort = object.value("queryPort").toInt();
    }
    
    signals:
        void queryDone(int id);
    };

class ServerListModel : public QAbstractTableModel
{
    Q_OBJECT
    
    QList<ServerEntry*> servers;
    QMap<QString, ServerEntry*> serverMap;
public:

    int playerCount() {
        int count = 0;
        for(auto server: servers) {
            if(server->ping != MAX_PING) {
                count += server->playerCount;
            }
        }
        return count;
    }
    
    int serverCount() {
        int count = 0;
        for(auto server: servers) {
            if(server->ping != MAX_PING)
                count++;
        }
        return count;
    }
    
    ServerListModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {
        qDebug() << setHeaderData(1, Qt::Horizontal, "PlayerCount");
        
        emit headerDataChanged(Qt::Horizontal, 0, (int)Column::MaxColumn-1);
    }

    void loadFromJson(QJsonObject object) {
        beginResetModel();
    
        QSet<QString> newServers;
        for(auto server : object.value("servers").toArray()) {
            auto object = server.toObject();
            auto address = object.value("address").toString();
            newServers.insert(address);
        }
        for(auto server: servers) {
            auto address = server->address();
            if(!newServers.contains(address)) {
                qDebug() << "REMOVING " << address;
                serverMap[address]->deleteLater();
                serverMap.remove(address);
            }
        }
        
        servers.clear();
        
        int id = 0;
        for(auto server : object.value("servers").toArray()) {
            auto object = server.toObject();            
            auto address = object.value("address").toString();
            
            auto entry = serverMap[address];
            if(!entry) {
                entry = new ServerEntry();
                serverMap[address] = entry;
            }
            entry->id = id++;
            servers.append(entry);
            
            entry->updateFromJson(object);
            
            connect(entry, &ServerEntry::queryDone, [=](int id) {
                emit dataChanged(createIndex(id, 0),createIndex(id, (int)Column::MaxColumn-1));
            });
            
            entry->query();
        }
        // redo ids

        endResetModel();
    }

    QString humanizeGameMode(QString mode) const {
        QRegExp rx("UT(.*)GameMode");
        if(rx.indexIn(mode) != -1) {
            return rx.cap(1);
        }
        return mode;
    }
    
    QVariant data(const QModelIndex& index, int role) const {
        auto entry = servers[index.row()];
        
        if(role == Qt::TextAlignmentRole) {
            return Qt::AlignCenter;
        }

        if(role == Qt::UserRole) {
            return QVariant::fromValue<ServerEntry*>(entry);
        }
        
        switch((Column)index.column()) {
            case Column::Country:
                if(role == Qt::DecorationRole)
                    return QPixmap(QString(":/flags/") + entry->countryCode + ".png");
                if(role == Qt::ToolTipRole )
                    return entry->countryCode;
                break;
            case Column::PlayerCount:
                if(role == Qt::DisplayRole)
                    return QString("%1/%2").arg(entry->playerCount).arg(entry->maxPlayerCount);
                break;
            case Column::GameMode:
                if(role == Qt::DisplayRole)
                    return humanizeGameMode(entry->gameMode);
                break;
            case Column::Map:
                if(role == Qt::DisplayRole)
                    return entry->map;
                break;
            case Column::Name:
                if(role == Qt::DisplayRole)
                    return entry->name;
                break;
            case Column::Ping:
                if(role == Qt::DisplayRole)
                    return entry->ping;
                break;
        }
        
        return QVariant();
    }
    
    QVariant headerData(int section, Qt::Orientation orientation, int role) const {
        static QStringList headers = {
            "", "Count", "Gametype", "Map", "Server Name", "Ping"
        };
        if(orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            return headers[section];
        }
        
        return QVariant();
    }

    
    int rowCount(const QModelIndex&) const {
        return servers.size();
    }
    
    int columnCount(const QModelIndex&) const {
        return (int)Column::MaxColumn;
    }
    const ServerEntry& entryById(int id) const {
        return *servers[id];
    }
    
};

#include <QSortFilterProxyModel>

class ServerListProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    ServerListModel* model;
public:
    ServerListProxyModel(ServerListModel* _model, QObject* parent = 0) : QSortFilterProxyModel(parent), model(_model) {
        
    }
 protected:
     bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
        
        auto& entry = model->entryById(sourceRow);
        if(entry.ping == MAX_PING)
            return false;

         QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
     }
     bool lessThan(const QModelIndex &left, const QModelIndex &right) const {
         if(left.column() == (int)Column::Country && right.column() == (int)Column::Country) {
             auto& leftEntry = model->entryById(left.row());
             auto& rightEntry = model->entryById(right.row());
             return QString::compare(leftEntry.countryCode, rightEntry.countryCode) < 0;
         }
         if(left.column() == (int)Column::Ping && right.column() == (int)Column::Ping) {
             auto& leftEntry = model->entryById(left.row());
             auto& rightEntry = model->entryById(right.row());
             
             return leftEntry.avgPing < rightEntry.avgPing;
         }
         
         QSortFilterProxyModel::lessThan(left, right);
     }
};

class iconned_dock_style: public QProxyStyle{
    Q_OBJECT
    QIcon icon_;
public:
    iconned_dock_style(const QIcon& icon,  QStyle* style = 0)
        : QProxyStyle(style)
        , icon_(icon)
    {}

    virtual ~iconned_dock_style()
    {}

    virtual void drawControl(ControlElement element, const QStyleOption* option,
        QPainter* painter, const QWidget* widget = 0) const
    {
        if(element == QStyle::CE_DockWidgetTitle)
        {
            //width of the icon
            int width = pixelMetric(QStyle::PM_ToolBarIconSize);
            //margin of title from frame
            int margin = baseStyle()->pixelMetric(QStyle::PM_DockWidgetTitleMargin);

            QPoint icon_point(margin + option->rect.left(), margin + option->rect.center().y() - width/2);

            painter->drawPixmap(icon_point, icon_.pixmap(width, width));

            const_cast<QStyleOption*>(option)->rect = option->rect.adjusted(width, 0, 0, 0);
        }
        baseStyle()->drawControl(element, option, painter, widget);
    }
};


#include <QDesktopWidget>
#include <QCloseEvent>

class ServerBrowser : public QMainWindow
{
    Q_OBJECT
    QTableView* table;
    ServerListModel* model;
    ServerListProxyModel proxyModel;
    bool m_editorSupport = false;

    QTableWidget* playerListWidget;
    QLabel* motdLabel;
    QLabel* statusLabel;
    
    QToolBar* buttonsToolbar;
    QAction* settingsAction;
    QAction* playAction;
    QAction* spectateAction;
    
    bool m_hideOnClose = false;
    
protected:
    void closeEvent(QCloseEvent* event) {
        if(m_hideOnClose) {
            event->ignore();
            this->hide();
            return;
        }
        QMainWindow::closeEvent(event);
    }
    
public:
    void setHideOnClose(bool status) {
        m_hideOnClose = status;
    }
    
    void loadFromJson(QJsonObject object) {
        model->loadFromJson(object);
    }
    void setMOTD(QString motd) {
        motdLabel->setText(motd);
    }
    
    ServerBrowser(QWidget* parent = nullptr) : model(new ServerListModel(this)), proxyModel(model, this), QMainWindow(parent) {
        table = new QTableView(this);
        
        setMinimumSize(QSize(1000, 580));
        
        setWindowIcon(QIcon(":/icon.png"));
        setWindowTitle(QString("UTLauncher %1.%2.%3").arg(VERSION_MAJOR).arg(VERSION_MINOR).arg(VERSION_PATCH));
        
        auto updatePlayersFromEntry = [=] (const ServerEntry& entry){
            playerListWidget->setRowCount(entry.players.length());
            
            auto players = entry.players;
            qSort(players.begin(), players.end());
            int row = 0;
            for(auto player: players) {
                auto item = new QTableWidgetItem(player.name);
                item->setTextAlignment(Qt::AlignCenter);
                item->setFlags(item->flags() ^ (Qt::ItemIsEditable));
                playerListWidget->setItem(row, 0, item);
                item = new QTableWidgetItem(QString::number(player.score));
                item->setTextAlignment(Qt::AlignCenter);
                item->setFlags(item->flags() ^ (Qt::ItemIsEditable));
                playerListWidget->setItem(row++, 1, item);
            }
        };
        
        
        {
            statusLabel = new QLabel("Status test", this);
            statusLabel->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
            statusLabel->setMinimumWidth(200);
            
            motdLabel = new QLabel("", this);
            motdLabel->setTextFormat(Qt::RichText);
            motdLabel->setAlignment(Qt::AlignHCenter|Qt::AlignVCenter);

            
            //motdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            motdLabel->setOpenExternalLinks(true);
            
            statusBar()->addPermanentWidget(motdLabel, 1);
            statusBar()->addPermanentWidget(statusLabel);
        }
        
        connect(model, &ServerListModel::dataChanged, [=](QModelIndex index, QModelIndex) {
            statusLabel->setText(QString("Total %1 servers / %2 players").arg(model->serverCount()).arg(model->playerCount()));
            if(index.row() == -1)
                return;
            auto selectedIndex = proxyModel.mapToSource(table->currentIndex());
            if(selectedIndex.row() == -1)
                return;
            auto& entry = model->entryById(index.row());
            if(selectedIndex.row() != index.row())
                return;
            updatePlayersFromEntry(entry);
            
        });
        
        
        proxyModel.setSourceModel(model);
        
        
        {
            auto toolbar = buttonsToolbar = new QToolBar("Actions", this);
            toolbar->setToolButtonStyle( Qt::ToolButtonTextUnderIcon );
            
            // toolbar->setToolButtonStyle( Qt::ToolButtonFollowStyle );
            {
                playAction = new QAction(awesome->icon(fa::gamepad, {
                    {"scale-factor", 0.8}
                }), "Play", this);
                playAction->setDisabled(true);
                toolbar->addAction(playAction);
                connect(playAction, &QAction::triggered, [=]() {
                    auto index = proxyModel.mapToSource(table->currentIndex());
                    if(index.row() == -1)
                        return;
                    auto& entry = model->entryById(index.row());
                    emit openServer(entry.host + ":" + QString::number(entry.port), false);
                });
            }
            {
                spectateAction = new QAction(awesome->icon(fa::eye, {
                    {"scale-factor", 0.8}
                }), "Spectate", this);
                spectateAction->setDisabled(true);
                toolbar->addAction(spectateAction);
                connect(spectateAction, &QAction::triggered, [=]() {
                    auto index = proxyModel.mapToSource(table->currentIndex());
                    if(index.row() == -1)
                        return;
                    auto& entry = model->entryById(index.row());
                    emit openServer(entry.host + ":" + QString::number(entry.port), true);
                });
            }
            
            toolbar->addSeparator();
            {
                settingsAction = new QAction(awesome->icon(fa::cogs, {
                    {"scale-factor", 0.8}
                }), "Settings", this);
                
                connect(settingsAction, &QAction::triggered, [=] {
                    emit openSettings();
                });
                toolbar->addAction(settingsAction);
            }
            
            addToolBar(Qt::LeftToolBarArea, toolbar);
        }
        {
            auto dockWidget = new QDockWidget("Currently playing", this);
            dockWidget->setStyle(new iconned_dock_style(awesome->icon(fa::user, {
                    {"scale-factor", 0.6}
                }), dockWidget->style() ));
            
            //auto toolbar = new QToolBar("Currently playing", this);
            
            auto widget = new QWidget(this);
            auto layout = new QVBoxLayout;
            
//             auto labelWidget = new QWidget(this);
//             {
//                 auto label = new QLabel(this);
//                 
//                 int dpiX = qApp->desktop()->logicalDpiX();
//                 float dpiScale = (float)dpiX / 96;
//                 label->setPixmap(awesome->icon(fa::users, {
//                     {"scale-factor", 0.8}
//                 }).pixmap(dpiScale*32, dpiScale*32));
//                 
//                                 
//                 auto layout = new QHBoxLayout;
//                 labelWidget->setLayout(layout);
//                 layout->addWidget(label);
//                 label->setFixedWidth(32*dpiScale + 16);
//                 label->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
//                 label = new QLabel("Currently playing", this);
//                 label->setAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
// 
//                 layout->addWidget(label);
//                 layout->setContentsMargins(QMargins(0, 0, 0, 0));
// //                layout->setSpacing(16);
//             }
            //layout->addWidget(labelWidget);
            playerListWidget = new QTableWidget(this);
            playerListWidget->setColumnCount(2);
            
            playerListWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
            playerListWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
            playerListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
            playerListWidget->horizontalHeader()->setStretchLastSection(true);
            playerListWidget->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::Stretch);
            playerListWidget->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Fixed);
            playerListWidget->setHorizontalHeaderLabels(QStringList() << "Name" << "Score");
            playerListWidget->horizontalHeader()->resizeSection(0, 150);
            
            layout->addWidget(playerListWidget);
            
            widget->setLayout(layout);
            
            
            layout->setContentsMargins(QMargins(0, 0, 0, 0));
            layout->setSpacing(0);
            //toolbar->setLayoutDirection(Qt::Vertical);
            
            //toolbar->addWidget(widget);
            dockWidget->setWidget(widget);
            addDockWidget(Qt::RightDockWidgetArea, dockWidget);
            //addToolBar(Qt::RightToolBarArea, toolbar);
        }
        
        table->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(table, &QTableView::customContextMenuRequested, [=](QPoint pos) {
            QModelIndex index = proxyModel.mapToSource(table->indexAt(pos));
            if(index.row() == -1)
                return;
            QMenu* menu = new QMenu(this);
            
            
            auto playAction = new QAction(awesome->icon( fa::gamepad ),"Play", this);
            menu->addAction(playAction);
            auto spectateAction = new QAction(awesome->icon( fa::eye ), "Spectate", this);
            menu->addAction(spectateAction);
            connect(playAction, &QAction::triggered, [=]() {
                auto& entry = model->entryById(index.row());
                emit openServer(entry.host + ":" + QString::number(entry.port));
            });
            connect(spectateAction, &QAction::triggered, [=]() {
                auto& entry = model->entryById(index.row());
                emit openServer(entry.host + ":" + QString::number(entry.port), true);
            });
            
            
            if(m_editorSupport) {
                menu->addSeparator();
                auto playAction = new QAction(awesome->icon( fa::gamepad ),"Play (Editor)", this);
                menu->addAction(playAction);
                auto spectateAction = new QAction(awesome->icon( fa::eye ), "Spectate (Editor)", this);
                menu->addAction(spectateAction);
                connect(playAction, &QAction::triggered, [=]() {
                    auto& entry = model->entryById(index.row());
                    emit openServer(entry.host + ":" + QString::number(entry.port), false, true);
                });
                connect(spectateAction, &QAction::triggered, [=]() {
                    auto& entry = model->entryById(index.row());
                    emit openServer(entry.host + ":" + QString::number(entry.port), true, true);
                });
                
            }
            
            
            menu->popup(table->viewport()->mapToGlobal(pos));
            
        });
        
        table->setSortingEnabled(true);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setModel(&proxyModel);
        table->horizontalHeader()->setStretchLastSection(true);
        proxyModel.setDynamicSortFilter(true);

        connect(table->selectionModel(), &QItemSelectionModel::currentRowChanged, [=](const QModelIndex & current, const QModelIndex & previous ) {
            auto index = proxyModel.mapToSource(current);
            
            qDebug() << "Selected row" << index.row();
            playAction->setDisabled(index.row()==-1);
            spectateAction->setDisabled(index.row()==-1);
            if(index.row() < 0) {
                playerListWidget->setRowCount(0);
                return;
            }
            auto& entry = model->entryById(index.row());
            updatePlayersFromEntry(entry);
        });

        
        proxyModel.setFilterKeyColumn((int)Column::Ping);
        #define STRINGIFY(s) #s
        proxyModel.setFilterRegExp("^(?!" STRINGIFY(MAX_PING) "$)\\d+");
        
        table->sortByColumn((int)Column::Ping, Qt::AscendingOrder);

        for(int i = 0;i < 4;++i)
            table->horizontalHeader()->setSectionResizeMode( i, QHeaderView::ResizeToContents);
        
        table->horizontalHeader()->resizeSection((int)Column::GameMode, 40);
        table->horizontalHeader()->setSectionResizeMode( (int)Column::Name, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode( (int)Column::Map, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode( (int)Column::Ping, QHeaderView::Fixed);
        table->horizontalHeader()->resizeSection((int)Column::Ping, 40);
        
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        
        
        connect(table, &QTableView::doubleClicked, [=](const QModelIndex& sortedIndex) {
            auto index = proxyModel.mapToSource(sortedIndex);
            auto& entry = model->entryById(index.row());
            emit openServer(entry.host + ":" + QString::number(entry.port));
            
        });
        
        setCentralWidget(table);
    }
    bool editorSupport() const {
        return m_editorSupport;
    }
    
    void showEvent(QShowEvent* show) {
        // make sure all the buttons have uniform size
        QList<QAction*> actions;
        actions << playAction << spectateAction << settingsAction;
        int maxWidth = 60;
        for(QAction* action: actions) {
            maxWidth = std::max(maxWidth, buttonsToolbar->widgetForAction(action)->width());
        }
        for(QAction* action: actions) {
            buttonsToolbar->widgetForAction(action)->setMinimumWidth(maxWidth);
        }
        QMainWindow::showEvent(show);
    }
    
public slots:
    void setEditorSupport(bool status) {
        m_editorSupport = status;
    }
signals:
    void openSettings();
    void openServer(QString url, bool spectate = false, bool inEditor = false);
};
