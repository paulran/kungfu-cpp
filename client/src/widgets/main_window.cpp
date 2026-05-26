#include "main_window.h"
#include "login_dialog.h"
#include "market_widget.h"
#include "order_entry_widget.h"
#include "order_widget.h"
#include "trade_widget.h"
#include "position_widget.h"
#include "asset_widget.h"
#include "strategy_widget.h"
#include "system_widget.h"
#include "api/rest_client.h"
#include "api/ws_client.h"
#include "models/order_model.h"
#include "models/trade_model.h"
#include "models/position_model.h"
#include "models/quote_model.h"
#include "models/process_model.h"

#include <QDockWidget>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QMessageBox>
#include <QLabel>

namespace kf {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , rest_(new RestClient(this))
    , ws_(new WsClient(this))
    , refresh_timer_(new QTimer(this))
    , order_model_(new OrderModel(this))
    , trade_model_(new TradeModel(this))
    , position_model_(new PositionModel(this))
    , quote_model_(new QuoteModel(this))
    , process_model_(new ProcessModel(this))
{
    setWindowTitle(QStringLiteral("KungFu Trading System"));
    resize(1400, 900);

    createDockWidgets();
    createMenuBar();
    connectSignals();

    refresh_timer_->setInterval(5000);
    connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::refreshAll);

    statusBar()->showMessage(QStringLiteral("未连接"));
}

MainWindow::~MainWindow() = default;

void MainWindow::createDockWidgets() {
    setDockNestingEnabled(true);

    // Market Data - top left
    market_widget_ = new MarketWidget(quote_model_);
    auto *marketDock = new QDockWidget(QStringLiteral("行情"), this);
    marketDock->setWidget(market_widget_);
    marketDock->setObjectName("market_dock");
    addDockWidget(Qt::TopDockWidgetArea, marketDock);

    // Order Entry - top right
    order_entry_widget_ = new OrderEntryWidget;
    auto *entryDock = new QDockWidget(QStringLiteral("下单"), this);
    entryDock->setWidget(order_entry_widget_);
    entryDock->setObjectName("order_entry_dock");
    addDockWidget(Qt::TopDockWidgetArea, entryDock);

    // Orders - bottom left
    order_widget_ = new OrderWidget(order_model_);
    auto *orderDock = new QDockWidget(QStringLiteral("委托"), this);
    orderDock->setWidget(order_widget_);
    orderDock->setObjectName("order_dock");
    addDockWidget(Qt::BottomDockWidgetArea, orderDock);

    // Trades - bottom
    trade_widget_ = new TradeWidget(trade_model_);
    auto *tradeDock = new QDockWidget(QStringLiteral("成交"), this);
    tradeDock->setWidget(trade_widget_);
    tradeDock->setObjectName("trade_dock");
    addDockWidget(Qt::BottomDockWidgetArea, tradeDock);

    // Positions - bottom
    position_widget_ = new PositionWidget(position_model_);
    auto *posDock = new QDockWidget(QStringLiteral("持仓"), this);
    posDock->setWidget(position_widget_);
    posDock->setObjectName("position_dock");
    addDockWidget(Qt::BottomDockWidgetArea, posDock);

    // Assets - right
    asset_widget_ = new AssetWidget;
    auto *assetDock = new QDockWidget(QStringLiteral("资金"), this);
    assetDock->setWidget(asset_widget_);
    assetDock->setObjectName("asset_dock");
    addDockWidget(Qt::RightDockWidgetArea, assetDock);

    // Strategy - right
    strategy_widget_ = new StrategyWidget;
    auto *stratDock = new QDockWidget(QStringLiteral("策略"), this);
    stratDock->setWidget(strategy_widget_);
    stratDock->setObjectName("strategy_dock");
    addDockWidget(Qt::RightDockWidgetArea, stratDock);

    // System - right
    system_widget_ = new SystemWidget(process_model_);
    auto *sysDock = new QDockWidget(QStringLiteral("系统"), this);
    sysDock->setWidget(system_widget_);
    sysDock->setObjectName("system_dock");
    addDockWidget(Qt::RightDockWidgetArea, sysDock);

    tabifyDockWidget(tradeDock, posDock);
    tabifyDockWidget(stratDock, sysDock);
}

void MainWindow::createMenuBar() {
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    auto *loginAct = fileMenu->addAction(QStringLiteral("登录(&L)..."));
    connect(loginAct, &QAction::triggered, this, &MainWindow::showLogin);
    fileMenu->addSeparator();
    auto *exitAct = fileMenu->addAction(QStringLiteral("退出(&X)"));
    connect(exitAct, &QAction::triggered, this, &QMainWindow::close);

    auto *viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    for (auto *dock : findChildren<QDockWidget *>()) {
        viewMenu->addAction(dock->toggleViewAction());
    }

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    auto *aboutAct = helpMenu->addAction(QStringLiteral("关于(&A)"));
    connect(aboutAct, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, QStringLiteral("关于 KungFu"),
                           QStringLiteral("KungFu Trading System\nQt Client v1.0"));
    });
}

void MainWindow::connectSignals() {
    // REST responses
    connect(rest_, &RestClient::ordersReceived, order_model_, &OrderModel::setOrders);
    connect(rest_, &RestClient::accountPositionsReceived, position_model_, &PositionModel::setPositions);
    connect(rest_, &RestClient::systemStatusReceived, process_model_, &ProcessModel::setProcesses);
    connect(rest_, &RestClient::accountAssetsReceived, asset_widget_, &AssetWidget::setAsset);
    connect(rest_, &RestClient::strategiesReceived, strategy_widget_, &StrategyWidget::setStrategies);
    connect(rest_, &RestClient::accountsReceived, this, [this](const QVector<AccountInfo> &accounts) {
        if (!accounts.isEmpty() && current_account_.isEmpty()) {
            current_account_ = accounts.first().account_id;
            rest_->getAccountAssets(current_account_);
            rest_->getAccountPositions(current_account_);
        }
        asset_widget_->setAccounts(accounts);
        position_widget_->setAccounts(accounts);
    });
    connect(rest_, &RestClient::orderSubmitted, this, [this](uint64_t id) {
        statusBar()->showMessage(QStringLiteral("订单已提交: %1").arg(id), 3000);
        rest_->getOrders();
    });
    connect(rest_, &RestClient::orderCancelled, this, [this](uint64_t id) {
        statusBar()->showMessage(QStringLiteral("撤单已提交: %1").arg(id), 3000);
        rest_->getOrders();
    });
    connect(rest_, &RestClient::requestError, this, [this](const QString &ep, const QString &err) {
        statusBar()->showMessage(QStringLiteral("错误 [%1]: %2").arg(ep, err), 5000);
    });

    // WebSocket real-time
    connect(ws_, &WsClient::quoteReceived, quote_model_, &QuoteModel::updateQuote);
    connect(ws_, &WsClient::orderReceived, order_model_, &OrderModel::updateOrder);
    connect(ws_, &WsClient::tradeReceived, trade_model_, &TradeModel::addTrade);
    connect(ws_, &WsClient::positionReceived, position_model_, &PositionModel::updatePosition);
    connect(ws_, &WsClient::assetReceived, asset_widget_, &AssetWidget::setAsset);
    connect(ws_, &WsClient::systemStatusChanged, process_model_, &ProcessModel::updateProcess);

    connect(ws_, &WsClient::connected, this, [this]() {
        statusBar()->showMessage(QStringLiteral("WebSocket 已连接"), 3000);
    });
    connect(ws_, &WsClient::disconnected, this, [this]() {
        statusBar()->showMessage(QStringLiteral("WebSocket 已断开，正在重连..."));
    });

    // Order entry submit
    connect(order_entry_widget_, &OrderEntryWidget::orderSubmitted, this, [this](const OrderInput &input) {
        rest_->submitOrder(input);
    });

    // Order cancel
    connect(order_widget_, &OrderWidget::cancelRequested, this, [this](uint64_t id) {
        rest_->cancelOrder(id);
    });

    // Market subscribe
    connect(market_widget_, &MarketWidget::subscribeRequested, this, [this](const QString &inst, const QString &exch) {
        rest_->subscribe(inst, exch);
        ws_->subscribe("quote." + inst);
    });

    // Account selection
    connect(asset_widget_, &AssetWidget::accountSelected, this, [this](const QString &acct) {
        current_account_ = acct;
        rest_->getAccountAssets(acct);
        rest_->getAccountPositions(acct);
    });
    connect(position_widget_, &PositionWidget::accountSelected, this, [this](const QString &acct) {
        current_account_ = acct;
        rest_->getAccountPositions(acct);
    });
}

void MainWindow::setAuthenticated(const QString &baseUrl, const QString &token) {
    rest_->setBaseUrl(baseUrl);
    rest_->setToken(token);

    QString wsUrl = baseUrl;
    wsUrl.replace("http://", "ws://").replace("https://", "wss://");
    if (!wsUrl.endsWith("/")) wsUrl += "/";
    // Standard WS endpoint is at /ws relative to base
    ws_->setUrl(wsUrl.left(wsUrl.length() - 1) + "/ws");

    ws_->connectToServer();
    ws_->subscribeAll();

    refresh_timer_->start();
    refreshAll();

    statusBar()->showMessage(QStringLiteral("已登录"));
}

void MainWindow::refreshAll() {
    rest_->getSystemStatus();
    rest_->getAccounts();
    rest_->getOrders();
    rest_->getStrategies();
    if (!current_account_.isEmpty()) {
        rest_->getAccountAssets(current_account_);
        rest_->getAccountPositions(current_account_);
    }
}

void MainWindow::showLogin() {
    auto *dlg = new LoginDialog(this);
    connect(dlg, &LoginDialog::loginRequested, this, [this, dlg](const QString &url, const QString &user, const QString &pass) {
        dlg->setLoading(true);
        rest_->setBaseUrl(url);

        auto loginConn = std::make_shared<QMetaObject::Connection>();
        auto failConn = std::make_shared<QMetaObject::Connection>();

        *loginConn = connect(rest_, &RestClient::loginSuccess, this, [this, dlg, url, loginConn, failConn](const QString &token) {
            disconnect(*loginConn);
            disconnect(*failConn);
            dlg->accept();
            setAuthenticated(url, token);
        });
        *failConn = connect(rest_, &RestClient::loginFailed, this, [dlg, loginConn, failConn](const QString &err) {
            disconnect(*loginConn);
            disconnect(*failConn);
            dlg->setLoading(false);
            dlg->setError(err);
        });

        rest_->login(user, pass);
    });
    dlg->exec();
    dlg->deleteLater();
}

} // namespace kf
