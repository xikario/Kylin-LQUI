#pragma once
#include <QDialog>
#include <QImage>
#include <QVector>
#include <QColor>
class QGraphicsView;
class QGraphicsScene;
class QGraphicsItem;
class QGraphicsPathItem;
class QLineEdit;
class QSlider;
class QPushButton;
class QLabel;
class QAction;
class QVBoxLayout;

class CaptureEditor final:public QDialog {
    Q_OBJECT
public:
    enum Tool { Select,Arrow,Ellipse,Rectangle,Text,Line,Freehand };
    explicit CaptureEditor(QImage source,QWidget *parent=nullptr,const QRect &selection=QRect());
    ~CaptureEditor() override;
    QImage renderedImage() const;
    int annotationCount() const{return marks.size();}
    void setProcessingAvailable(bool available);
    static QString actionPrompt(const QString &action,const QString &language);
    QImage resultImage() const{return output;}
    QString selectedAction() const{return action;}
protected:
    bool eventFilter(QObject *,QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
private:
    void setTool(Tool tool);
    void positionTools();
    void fitImage();
    void finish(const QString &action);
    void undo();
    void redo();
    void clearRedo();
    void refreshUndo();
    QPointF imagePoint(const QPoint &) const;
    void updateStroke(QPointF end);
    QImage source,output;
    QString action;
    QGraphicsView *view=nullptr;
    QGraphicsScene *scene=nullptr;
    QGraphicsPathItem *stroke=nullptr;
    QVector<QGraphicsItem*> marks,undone;
    QVector<QPointF> points;
    QPointF first;
    Tool tool=Select;
    QColor color=QColor("#ef5350");
    QLineEdit *text;
    QSlider *thickness;
    QPushButton *undoButton,*redoButton;
    QList<QPushButton*> processing;
    QWidget *toolbar=nullptr,*options=nullptr;
    QVBoxLayout *layout=nullptr;
    QRect anchor,workArea;
    bool placing=false;
    QLabel *status;
};
