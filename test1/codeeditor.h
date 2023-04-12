#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include <QPlainTextEdit>


class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);

    void lineNumberAreaPainterEvent(QPaintEvent *event);

    int lineNumberAreaWidth();// 更新行号部分的宽度，并且设置视口部件的margin

protected:
    void resizeEvent(QResizeEvent *e) override;

private slots:
    void updateLineNumerAreaWidth(int newBlockCount);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect &rect,int dy);

private:
    QWidget *lineNumberArea;

};


#endif // CODEEDITOR_H
