#pragma once

#include <QMainWindow>
class RendererBridge;
class PlayerController;
class PlayerWidget;

class MainWindow final: public QMainWindow {
    Q_OBJECT


public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public Q_SLOTS:
    void OnSliderValueChanged(int);
    void OnSliderPressed() const;
    void OnSliderValueReleased() const;

private:
    PlayerWidget *mRender{};
    PlayerController *mController{};
    QTimer *mProgressTimer{};
    int64_t mCurrentPos;
    int64_t mTotalPos;
    struct QSlider* mProgressBar;
    struct QListView* mListView;
};
