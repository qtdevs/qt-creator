/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#include "outputformatter.h"

#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>

#include <QtGui/QPlainTextEdit>

using namespace ProjectExplorer;
using namespace TextEditor;

OutputFormatter::OutputFormatter()
    : QObject()
    , m_formats(0)
{

}

OutputFormatter::~OutputFormatter()
{
    if (m_formats)
        delete[] m_formats;
}

QPlainTextEdit *OutputFormatter::plainTextEdit() const
{
    return m_plainTextEdit;
}

void OutputFormatter::setPlainTextEdit(QPlainTextEdit *plainText)
{
    m_plainTextEdit = plainText;
    initFormats();
}

void OutputFormatter::appendApplicationOutput(const QString &text, bool onStdErr)
{
    append(text, onStdErr ? StdErrFormat : StdOutFormat);
}

void OutputFormatter::appendMessage(const QString &text, bool isError)
{
    append(text, isError ? ErrorMessageFormat : NormalMessageFormat);
}

void OutputFormatter::append(const QString &text, Format format)
{
    append(text, m_formats[format]);
}

void OutputFormatter::append(const QString &text, const QTextCharFormat &format)
{
    QTextCursor cursor(m_plainTextEdit->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text, format);
}

static QColor mix_colors(QColor a, QColor b)
{
    return QColor((a.red() + 2 * b.red()) / 3, (a.green() + 2 * b.green()) / 3,
                  (a.blue() + 2* b.blue()) / 3, (a.alpha() + 2 * b.alpha()) / 3);
}


void OutputFormatter::initFormats()
{
    QPalette p = plainTextEdit()->palette();

    FontSettings fs = TextEditorSettings::instance()->fontSettings();
    QFont font = fs.font();
    QFont boldFont = font;
    boldFont.setBold(true);

    m_formats = new QTextCharFormat[NumberOfFormats];

    // NormalMessageFormat
    m_formats[NormalMessageFormat].setFont(boldFont);
    m_formats[NormalMessageFormat].setForeground(mix_colors(p.color(QPalette::Text), QColor(Qt::blue)));

    // ErrorMessageFormat
    m_formats[ErrorMessageFormat].setFont(boldFont);
    m_formats[ErrorMessageFormat].setForeground(mix_colors(p.color(QPalette::Text), QColor(Qt::red)));

    // StdOutFormat
    m_formats[StdOutFormat].setFont(font);
    m_formats[StdOutFormat].setForeground(p.color(QPalette::Text));

    // StdErrFormat
    m_formats[StdErrFormat].setFont(font);
    m_formats[StdErrFormat].setForeground(mix_colors(p.color(QPalette::Text), QColor(Qt::red)));
}

void OutputFormatter::handleLink(const QString &href)
{
    Q_UNUSED(href);
}
