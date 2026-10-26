#pragma once
#include <QString>

// The launcher uses original artwork and a farm-inspired palette. Semantic
// role properties let primary actions and section headings keep clear emphasis.
inline QString launcherStyle() {
    return QStringLiteral(R"(
        QWidget {
            background: #f2e6c2;
            color: #493a28;
            font-family: "sans-serif";
            font-size: 13px;
        }
        QLabel { background: transparent; }
        QLabel[role="section"] {
            color: #48643a;
            font-weight: bold;
            padding: 4px 0;
        }
        QPushButton {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #f0d291, stop:1 #dcb374);
            color: #493522;
            padding: 10px 14px;
            border: 2px solid #a17b48;
            border-radius: 7px;
            font-weight: bold;
        }
        QPushButton:hover { background: #f8dda1; border-color: #7d623b; }
        QPushButton:pressed { background: #cfa266; }
        QPushButton[role="primary"] {
            background: #607e42;
            color: #fff8dc;
            border-color: #415b30;
        }
        QPushButton[role="primary"]:hover { background: #73954e; }
        QPushButton[role="primary"]:pressed { background: #4a6535; }
        QPushButton:disabled {
            color: #8c8066;
            background: #e1d6b8;
            border-color: #baad8e;
        }
        QListWidget, QTextEdit, QLineEdit, QComboBox {
            background: #fff9e2;
            color: #493a28;
            border: 2px solid #b79965;
            border-radius: 6px;
            padding: 8px;
            selection-background-color: #dbe5b2;
            selection-color: #33472a;
        }
        QListWidget::item { padding: 5px 4px; }
        QListWidget::item:selected {
            background: #dbe5b2;
            color: #33472a;
            border-radius: 4px;
        }
        QListWidget::item:hover { background: #f0edca; }
        QLineEdit:focus, QTextEdit:focus, QListWidget:focus,
        QComboBox:focus, QPushButton:focus { border: 2px solid #50713b; }
        QScrollBar:vertical { background: #ecdfb9; width: 14px; margin: 2px; }
        QScrollBar::handle:vertical { background: #b89a64; min-height: 28px; border-radius: 5px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar:horizontal { background: #ecdfb9; height: 14px; margin: 2px; }
        QScrollBar::handle:horizontal { background: #b89a64; min-width: 28px; border-radius: 5px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
        QToolTip { background: #fff8d9; color: #493a28; border: 1px solid #a17b48; padding: 6px; }
        QTabWidget::pane { border: 2px solid #b79965; background: #fff9e2; }
        QTabBar::tab { background: #e7d5a5; padding: 9px 18px; border: 1px solid #b79965; }
        QTabBar::tab:selected { background: #fff9e2; color: #3f5c32; font-weight: bold; }
        QGroupBox { border: 2px solid #b79965; border-radius: 7px; margin-top: 12px; padding-top: 12px; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #48643a; }
        QProgressBar { background: #fff9e2; border: 1px solid #a17b48; border-radius: 4px; text-align: center; }
        QProgressBar::chunk { background: #9ebc68; }
    )");
}
