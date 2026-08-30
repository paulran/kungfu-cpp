// SPDX-License-Identifier: Apache-2.0

#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>

namespace {

constexpr const char *kQss = R"!(
/* ===== 全局 ===== */
QWidget {
  font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
  font-size: 13px;
  color: #2c3e50;
}
QMainWindow, QDialog { background: #f5f7fa; }

/* ===== 顶部连接条 / 通用分组 ===== */
QGroupBox {
  background: #ffffff;
  border: 1px solid #dde3ea;
  border-radius: 6px;
  margin-top: 14px;
  padding: 8px;
  font-weight: 600;
  color: #4a5568;
}
QGroupBox::title {
  subcontrol-origin: margin;
  subcontrol-position: top left;
  left: 10px;
  padding: 0 6px;
  background: #f5f7fa;
}

/* ===== 按钮 ===== */
QPushButton {
  background: #ffffff;
  border: 1px solid #c7d2dc;
  border-radius: 4px;
  padding: 6px 14px;
  color: #2c3e50;
}
QPushButton:hover { border-color: #4ea0d8; color: #2f6fb0; }
QPushButton:pressed { background: #e6eef5; }
QPushButton:disabled { color: #b0b7c0; border-color: #e0e4ea; background: #f0f2f5; }
QPushButton#primaryBtn { background: #2f6fb0; color: #ffffff; border: none; }
QPushButton#primaryBtn:hover { background: #3a82c4; }
QPushButton#primaryBtn:disabled { background: #9fb6cf; color: #f0f2f5; }
QPushButton#buyBtn { background: #27ae60; color: #ffffff; border: none; font-weight: 600; }
QPushButton#buyBtn:hover { background: #2ecc71; }
QPushButton#buyBtn:disabled { background: #a3d9b8; color: #f0f2f5; }
QPushButton#sellBtn { background: #c0392b; color: #ffffff; border: none; font-weight: 600; }
QPushButton#sellBtn:hover { background: #e74c3c; }
QPushButton#sellBtn:disabled { background: #d9a39c; color: #f0f2f5; }

/* ===== 表格 ===== */
QTableWidget {
  background: #ffffff;
  alternate-background-color: #f3f6fa;
  gridline-color: #e6ebf0;
  border: 1px solid #dde3ea;
  border-radius: 4px;
  selection-background-color: #cfe3f3;
  selection-color: #1a2733;
}
QTableWidget::item { padding: 3px 6px; }
QHeaderView::section {
  background: #eef2f6;
  color: #4a5568;
  border: none;
  border-right: 1px solid #dde3ea;
  border-bottom: 1px solid #dde3ea;
  padding: 6px;
  font-weight: 600;
}
QTableCornerButton::section { background: #eef2f6; border: none; }

/* 卖盘/买盘（背景由 item 着色，这里只设表头与网格） */
QTableWidget#askBook, QTableWidget#bidBook {
  gridline-color: #f0d7d2;
  border: 1px solid #ecbcb4;
  border-radius: 4px;
  font-weight: 600;
}
QTableWidget#bidBook { gridline-color: #c9ead7; border-color: #b6e0c6; }

/* 行情 BOOK 标题与最新价 */
QLabel#bookTitle { font-size: 15px; font-weight: 700; color: #2f6fb0; }
QLabel#lastPrice {
  font-size: 16px; font-weight: 700; color: #1a2733;
  background: #eef2f6; border: 1px solid #dde3ea; border-radius: 4px;
  padding: 4px;
}

/* ===== 输入控件 ===== */
QLineEdit, QComboBox {
  padding: 5px 8px;
  border: 1px solid #c7d2dc;
  border-radius: 3px;
  background: #ffffff;
  selection-background-color: #2f6fb0;
}
QLineEdit:focus, QComboBox:focus { border-color: #4ea0d8; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
  background: #ffffff; border: 1px solid #c7d2dc; selection-background-color: #cfe3f3;
}

/* ===== 标签页 ===== */
QTabWidget::pane {
  border: 1px solid #dde3ea;
  border-radius: 4px;
  background: #ffffff;
}
QTabBar::tab {
  background: #eef2f6;
  color: #4a5568;
  padding: 6px 14px;
  border: 1px solid #dde3ea;
  border-bottom: none;
  border-top-left-radius: 4px;
  border-top-right-radius: 4px;
  margin-right: 2px;
}
QTabBar::tab:selected { background: #ffffff; color: #2f6fb0; font-weight: 600; }
QTabBar::tab:hover:!selected { background: #e2e8f0; }

/* ===== 日志（暗色终端） ===== */
QPlainTextEdit#logView {
  background: #1e2530;
  color: #c8d3e0;
  border: 1px solid #2a3540;
  border-radius: 4px;
  font-family: "Cascadia Mono", "Consolas", monospace;
  font-size: 12px;
}

/* ===== 分割条 ===== */
QSplitter::handle { background: #e2e8f0; }
QSplitter::handle:horizontal { width: 4px; }
QSplitter::handle:vertical { height: 4px; }

/* ===== 连接状态标签 ===== */
QLabel#statusLabel { font-weight: 600; padding: 2px 8px; }
QLabel#statusLabel[connected="true"] { color: #27ae60; }
QLabel#statusLabel[connected="false"] { color: #9e9e9e; }
)!";

}  // namespace

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("kf_client"));
  QApplication::setOrganizationName(QStringLiteral("kungfu-cpp"));
  app.setStyleSheet(QString::fromLatin1(kQss));
  // 运行时窗口/任务栏图标（exe 图标由 app.rc 嵌入为资源 ID 1）。
  app.setWindowIcon(QIcon(app.applicationDirPath() + QStringLiteral("/app.ico")));

  kfclient::MainWindow window;
  window.show();

  return app.exec();
}
