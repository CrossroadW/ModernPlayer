#pragma once

#include <QMainWindow>
class RendererBridge;
class PlayerController;
class PlayerWidget;
class QSlider;
class QListView;
class QPushButton;
class QTimer;
class OpenglPlayWidget;
// #define useglwidget
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public Q_SLOTS:
    void OnSliderValueChanged(int);
    void OnSliderPressed() const;
    void OnSliderValueReleased() const;

private:
#ifdef useglwidget
    OpenglPlayWidget *mRender{};
#else
    PlayerWidget *mRender{};
#endif
    PlayerController *mController{};
    QTimer *mProgressTimer{};
    int64_t mCurrentPos;
    int64_t mTotalPos;
    QSlider *mProgressBar;
    QListView *mListView;
    QPushButton *mPlayButton;
};
