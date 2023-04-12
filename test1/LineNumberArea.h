#ifndef LINENUMBERAREA_H
#define LINENUMBERAREA_H

#include "QtWidgets/qwidget.h"
#include "codeeditor.h"
class LineNumberArea : public QWidget
{
    Q_OBJECT

public:
    LineNumberArea(CodeEditor *editor) : QWidget(editor),codeEditor(editor){}
protected:
    // 控件的重绘工作，放到了codeEditor里面
    // 主要是因为需要取得文本块的序号和视口的大小，写在codeEditor里，参数调用会更加方便
    void paintEvent(QPaintEvent *event) override{
        codeEditor->lineNumberAreaPainterEvent(event);
    }

private:
    CodeEditor *codeEditor;
};

#endif // LINENUMBERAREA_H
