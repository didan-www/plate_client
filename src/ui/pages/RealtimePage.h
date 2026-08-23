#pragma once

#include <QPixmap>
#include <QWidget>

class ImageLoadResult;
class ImageLoadService;
class QLabel;
class QPushButton;
class QResizeEvent;
class QTableView;
class RecognitionTableModel;
class RealtimeRecognitionStore;

class RealtimePage final : public QWidget
{
    Q_OBJECT

public:
    RealtimePage(RealtimeRecognitionStore *store,
                 ImageLoadService *imageLoadService,
                 QWidget *parent = nullptr);
    ~RealtimePage() override;

    [[nodiscard]] RealtimeRecognitionStore *store() const noexcept;
    [[nodiscard]] ImageLoadService *imageLoadService() const noexcept;
    [[nodiscard]] RecognitionTableModel *tableModel() const noexcept;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void refreshCurrent();
    void applyImageResult(const ImageLoadResult &result);
    void retryImage();
    void clearImage(const QString &placeholder);
    void updateScaledPixmap();

    RealtimeRecognitionStore *store_;
    ImageLoadService *imageLoadService_;
    RecognitionTableModel *tableModel_;
    QLabel *imageLabel_;
    QLabel *statusLabel_;
    QLabel *plateLabel_;
    QLabel *errorLabel_;
    QLabel *capturedAtLabel_;
    QLabel *durationLabel_;
    QPushButton *retryButton_;
    QTableView *tableView_;
    QString currentRecognitionId_;
    QPixmap sourcePixmap_;
};
