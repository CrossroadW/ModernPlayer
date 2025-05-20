#include "MainWindow.h"
#include <QSplitter>
#include <QDebug>
#include <QMenuBar>
#include "PlayerWidget.h"
#include "PlayerController.h"
#include <qcoreapplication.h>
#include <qevent.h>
#include <QFileDialog>
#include <QPushButton>
#include <QHBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <spdlog/spdlog.h>
#include <QTimer>
#include <QInputDialog>
#include <QFile>
#include <QListView>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QDateTime>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    this->resize({800, 600});
    this->setMenuBar(new QMenuBar{});
    mProgressTimer = new QTimer(this);

    auto onStateChanged = [this] {
        spdlog::info("OnStateChanged");
        if (!mProgressTimer) {
            spdlog::warn("mProgressTimer is null");
            return;
        }
        if (mController->state() == PlayerState::Playing) {
            mPlayButton->setProperty("isPlaying", true);
            g::updateStyle(mPlayButton);
            mProgressTimer->start();
        } else if (mController->state() != PlayerState::Playing) {
            mPlayButton->setProperty("isPlaying", false);
            g::updateStyle(mPlayButton);
            mProgressTimer->stop();
        }
    };
    auto widget = new QWidget{};
    QFile qss(":/res/qss.qss");
    if (!qss.open(QFile::ReadOnly)) {
        spdlog::error("open qss.qss error");
        throw std::runtime_error("open qss.qss error");
    };
    widget->setStyleSheet(qss.readAll());
    setCentralWidget(widget);
    auto layout = new QVBoxLayout(widget);
    mRender = new PlayerWidget{};

    {
        // 创建 QListView 和 Model
        mListView = new QListView{};
        auto playListModel = new QStandardItemModel{mListView};
        playListModel->setItemRoleNames({}); // 可选，清空自定义角色名
        mListView->setEditTriggers(QAbstractItemView::NoEditTriggers); // 禁止双击编辑
        mListView->setDragEnabled(false); // 禁止拖动
        mListView->setDragDropMode(QAbstractItemView::NoDragDrop); // 不支持拖拽模式
        mListView->setDefaultDropAction(Qt::IgnoreAction); // 忽略拖拽操作
        mListView->setModel(playListModel);

        // 使用 QSplitter 实现可拖动布局
        auto *splitter = new QSplitter(Qt::Horizontal);
        splitter->addWidget(mListView);
        splitter->addWidget(mRender);

        // 设置默认比例
        splitter->setStretchFactor(0, 2);
        splitter->setStretchFactor(1, 5);
        splitter->setCollapsible(1, false); // 禁止折叠右侧

        // 设置最小宽度
        mListView->setMinimumWidth(100);
        mRender->setMinimumWidth(450);
        layout->addWidget(splitter);

        // 工具栏按钮控制展开/收缩
        auto *toolBar = addToolBar("控制");
        QAction *toggleListAction = new QAction("隐藏列表", this);
        QAction *takeScreenshotAction = new QAction("截图", this);
        toolBar->addAction(toggleListAction);
        toolBar->addAction(takeScreenshotAction);

        connect(takeScreenshotAction, &QAction::triggered, this, [=]() {
            QString picturesDir = QStandardPaths::writableLocation(
                QStandardPaths::PicturesLocation);

            // 创建一个带时间戳的文件名
            QString fileName = QDateTime::currentDateTime().toString(
                "screenshot_yyyyMMdd_HHmmss.png");
            QString filePath = picturesDir + "/" + fileName;

            // 保存截图
            if (mRender && mRender->grab().save(filePath)) {
                spdlog::info("截图成功：{}", filePath.toStdString());
            } else {
                spdlog::error("截图失败");
            }
        });
        // 保存初始展开尺寸
        int savedListWidth = mListView->width(); // 用于恢复时使用
        connect(toggleListAction, &QAction::triggered, this, [=]() mutable {
            QList<int> sizes = splitter->sizes();
            if (sizes[0] > 5) {
                // 当前是展开状态 -> 折叠（设置为 0 宽度）
                savedListWidth = sizes[0]; // 保存当前宽度
                splitter->setSizes({0, sizes[1] + sizes[0]});
                toggleListAction->setText("展开列表");
            } else {
                // 当前是折叠状态 -> 展开（恢复原来宽度）
                splitter->setSizes({savedListWidth, sizes[1] - savedListWidth});
                toggleListAction->setText("隐藏列表");
            }
        });

        auto *openFolderAction = new QAction("打开文件夹", this);
        toolBar->addAction(openFolderAction);
        connect(openFolderAction, &QAction::triggered, this, [=]() {
            QString dirPath =
                QFileDialog::getExistingDirectory(this, "选择视频文件夹");
            if (dirPath.isEmpty())
                return;

            QDir dir(dirPath);
            QStringList videoFilters = {"*.mp4", "*.mkv", "*.avi", "*.mov",
                                        "*.flv"};
            QFileInfoList fileList = dir.entryInfoList(
                videoFilters, QDir::Files);

            auto model = qobject_cast<QStandardItemModel *>(mListView->model());
            if (!model)
                return;

            model->clear(); // 清空旧列表

            for (const QFileInfo &fileInfo: fileList) {
                QStandardItem *item = new QStandardItem(fileInfo.fileName());
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                item->setData(fileInfo.absoluteFilePath(), Qt::UserRole);
                // 储存完整路径
                model->appendRow(item);
            }
        });

        connect(mListView, &QListView::clicked, this,
                [=](const QModelIndex &index) {
                    auto model = qobject_cast<QStandardItemModel *>(
                        mListView->model());
                    if (!model || !mController)
                        return;
                    if (mController->state() != PlayerState::Idle) {
                        delete mController;
                        mController = new PlayerController{mRender};
                    }
                    QString filePath = model->itemFromIndex(index)->data(
                        Qt::UserRole).toString();
                    spdlog::info("open file:{}", filePath.toStdString());
                    mController->Open(filePath.toStdString());
                    mController->Play();
                    connect(mController, &PlayerController::StateChanged, this,
                            onStateChanged);
                    onStateChanged();
                });
    }

    mController = new PlayerController{mRender};

    auto buttom = new QHBoxLayout{};

    mPlayButton = new QPushButton{};
    mPlayButton->setObjectName("playBtn");
    mPlayButton->setProperty("isPlaying", false); // 初始状态为暂停
    g::updateStyle(mPlayButton);
    auto before = new QPushButton{"后退5"};
    before->setObjectName("beforeBtn");
    auto after = new QPushButton{"快进5"};
    after->setObjectName("afterBtn");
    auto closeBnt = new QPushButton{};
    closeBnt->setObjectName("closeBtn");
    auto speedBtn = new QPushButton{"2x"};
    speedBtn->setObjectName("speedBtn");
    speedBtn->setCheckable(true);
    speedBtn->setToolTip("切换 2 倍速");

    mProgressBar = new QSlider{Qt::Horizontal};
    mProgressBar->setRange(0, 1000);
    mProgressBar->setValue(0);
    buttom->addWidget(before);
    buttom->addWidget(mPlayButton);
    buttom->addWidget(after);
    buttom->addWidget(speedBtn);
    buttom->addWidget(mProgressBar);
    buttom->addWidget(closeBnt);
    layout->addLayout(buttom);
    connect(speedBtn, &QPushButton::clicked, this, [this](bool checked) {
        if (!mController) {
            spdlog::warn("mController is null");
            return;
        }
        mController->Speed(checked);
    });
    connect(closeBnt, &QPushButton::clicked, this, [this] {
        spdlog::info("close");
        if (!mController || mController->state() == PlayerState::Idle) {
            return;
        }
        mController->Close();
        mProgressTimer->stop();
        mProgressBar->setValue(0);
    });
    connect(mProgressTimer, &QTimer::timeout, this, [this] {
        if (mController->state() == PlayerState::Playing) {
            auto [curr, total] = mController->CurrentPosition(); // 单位：毫秒

            // 设置进度条，范围 0 ~ 1000
            mProgressBar->setValue(static_cast<int>(1.0 * curr / total * 1000));

            mCurrentPos = curr;
            mTotalPos = total;

            // 当前时间（分钟、秒、小数秒）
            double curr_sec = (curr % 60000) / 1000.0;
            int curr_min = curr / 60000;

            double total_sec = (total % 60000) / 1000.0;
            int total_min = total / 60000;

            // 格式化显示到小数点后一位
            QString msg = QString::fromStdString(fmt::format(
                "{:02}:{:04.1f} / {:02}:{:04.1f}",
                curr_min, curr_sec,
                total_min, total_sec));

            if (auto statusBar = this->statusBar()) {
                statusBar->showMessage(msg);
            }
        }
    });

    connect(mPlayButton, &QPushButton::clicked, this, [this] {
        spdlog::info("playBtn");
        mController->Play();

        mProgressTimer->start(500);
    });
    auto seekOffset = 5 * 1000;

    connect(before, &QPushButton::clicked, this, [this,seekOffset] {
        if (mCurrentPos - seekOffset < 0) {
            mController->SeekTo(0);
        } else {
            mController->SeekTo(mCurrentPos - seekOffset);
        }
    });
    connect(after, &QPushButton::clicked, this, [this,seekOffset] {
        if (mCurrentPos + seekOffset > mTotalPos) {
            mController->SeekTo(mTotalPos - 1000);
        } else {
            mController->SeekTo(mCurrentPos + seekOffset);
        }
    });
    connect(mProgressBar, &QSlider::valueChanged, this,
            &MainWindow::OnSliderValueChanged);
    connect(mProgressBar, &QSlider::sliderPressed, this,
            &MainWindow::OnSliderPressed);
    connect(mProgressBar, &QSlider::sliderReleased, this,
            &MainWindow::OnSliderValueReleased);
    connect(mController, &PlayerController::StateChanged, this, [this] {
        if (!mProgressTimer) {
            spdlog::warn("mProgressTimer is null");
            return;
        }
        if (mController->state() == PlayerState::Playing) {
            mPlayButton->setProperty("isPlaying", true);
            g::updateStyle(mPlayButton);
            mProgressTimer->start();
        } else if (mController->state() != PlayerState::Playing) {
            mPlayButton->setProperty("isPlaying", false);
            g::updateStyle(mPlayButton);
            mProgressTimer->stop();
        }
    });

    auto file = menuBar()->addMenu("文件");
    auto onOpen = file->addAction("打开文件");
    connect(onOpen, &QAction::triggered, this, [this, onStateChanged] {
        spdlog::info("open file");
        auto filePath = QFileDialog::getOpenFileName(this, "Open File",
            "",
            "Video Files (*.mp4)");
        try {
            if (mController->state() != PlayerState::Idle) {
                delete mController;
                mController = new PlayerController{mRender};
            }
            mController->Open(filePath.toStdString());
            connect(mController, &PlayerController::StateChanged, this,
                    onStateChanged);
            onStateChanged();
        } catch (const std::exception &e) {
            spdlog::error("open file error:{}", e.what());
        }
    });
    auto onUrl = file->addAction("通过URL打开");
    connect(onUrl, &QAction::triggered, [this,onStateChanged] {
        bool ok;
        QString url = QInputDialog::getText(this,
                                            "Open URL",
                                            "Enter video URL:",
                                            QLineEdit::Normal,
                                            "http://",
                                            &ok);

        if (ok && !url.isEmpty()) {
            try {
                if (mController->state() != PlayerState::Idle) {
                    delete mController;
                    mController = new PlayerController{mRender};
                }
                mController->Open(url.toStdString());
                connect(mController, &PlayerController::StateChanged, this,
                        onStateChanged);
                onStateChanged();
            } catch (const std::exception &e) {
                spdlog::error("open url error: {}", e.what());
            }
        }
    });

    // add  status bar
    auto statusBar = new QStatusBar{};
    setStatusBar(statusBar);
    statusBar->showMessage("Ready");
}

MainWindow::~MainWindow() {
    if (mRender) {
        delete mRender;
    }
    if (mController) {
        delete mController;
    }
    if (mProgressTimer) {
        delete mProgressTimer;
    }
}

void MainWindow::OnSliderValueChanged(int value) {}

void MainWindow::OnSliderPressed() const {
    if (mController->state() == PlayerState::Idle) {
        spdlog::warn("OnSliderPressed PlayerState::Idle");

        return;
    }
    mProgressTimer->stop();
}

void MainWindow::OnSliderValueReleased() const {
    if (mController->state() == PlayerState::Idle) {
        spdlog::warn("OnSliderValueReleased PlayerState::Idle");
        mProgressBar->setValue(0);
        return;
    }
    int value = mProgressBar->value();
    mController->SeekTo(value * 1.0 / 1000 * mTotalPos);
    mProgressTimer->start();
}
