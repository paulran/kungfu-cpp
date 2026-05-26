#pragma once

#include <QMainWindow>
#include <QTimer>

namespace kf {

class RestClient;
class WsClient;
class OrderModel;
class TradeModel;
class PositionModel;
class QuoteModel;
class ProcessModel;

class MarketWidget;
class OrderEntryWidget;
class OrderWidget;
class TradeWidget;
class PositionWidget;
class AssetWidget;
class StrategyWidget;
class SystemWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void setAuthenticated(const QString &baseUrl, const QString &token);

public:
    void showLogin();

private:
    void createDockWidgets();
    void createMenuBar();
    void connectSignals();
    void refreshAll();

    RestClient *rest_;
    WsClient *ws_;
    QTimer *refresh_timer_;

    OrderModel *order_model_;
    TradeModel *trade_model_;
    PositionModel *position_model_;
    QuoteModel *quote_model_;
    ProcessModel *process_model_;

    MarketWidget *market_widget_;
    OrderEntryWidget *order_entry_widget_;
    OrderWidget *order_widget_;
    TradeWidget *trade_widget_;
    PositionWidget *position_widget_;
    AssetWidget *asset_widget_;
    StrategyWidget *strategy_widget_;
    SystemWidget *system_widget_;

    QString current_account_;
};

} // namespace kf
