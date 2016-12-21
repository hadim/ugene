/**
 * UGENE - Integrated Bioinformatics Tools.
 * Copyright (C) 2008-2016 UniPro <ugene@unipro.ru>
 * http://ugene.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "TableWidget.h"

namespace U2 {

static const int MIN_ROW_COUNT = 3;

#if (QT_VERSION < 0x050400) //Qt 5.7
TableWidget::TableWidget(const QWebElement &container, Dashboard *parent)
#else
TableWidget::TableWidget(const QString &container, Dashboard *parent)
#endif
: DashboardWidget(container, parent), useEmptyRows(true)
{

}

void TableWidget::createTable() {
#if (QT_VERSION < 0x050400) //Qt 5.7
    QString table = "<table class=\"table table-bordered table-fixed\">";
    foreach (int w, widths()) {
        table += "<col width=\"" + QString("%1").arg(w) + "%\" />";
    }
    table += "<thead><tr>";
    foreach (const QString &h, header()) {
        table += "<th><span class=\"text\">" + h + "</span></th>";
    }
    table += "</tr></thead>";
    table += "<tbody scroll=\"yes\"/>";
    table += "</table>";
    container.setInnerXml(table);
    rows.clear();
#else
    dashboard->page()->runJavaScript("createTable(\"" + container + "\", " + Jsutils::toJSArray(widths()) + "," + Jsutils::toJSArray(header()) + ");");
#endif
    if (useEmptyRows) {
        addEmptyRows();
    }
}


void TableWidget::fillTable() {
#if (QT_VERSION < 0x050400) //Qt 5.7
    rows.clear();
#endif
    foreach (const QStringList &row, data()) {
        addRow(row.first(), row.mid(1));
    }
}

void TableWidget::addEmptyRows() {
#if (QT_VERSION < 0x050400) //Qt 5.7
    int rowIdx = rows.size();
    QWebElement body = container.findFirst("tbody");
    while (rowIdx < MIN_ROW_COUNT) {
        QString row = "<tr class=\"empty-row\">";
        foreach(const QString &h, header()) {
            Q_UNUSED(h);
            row += "<td>&nbsp;</td>";
        }
        row += "</tr>";
        body.appendInside(row);
        rowIdx++;
    }
#else
    for (int rowIdx = 0; rowIdx <= MIN_ROW_COUNT; rowIdx++) {
        dashboard->page()->runJavaScript("addEmptyRows(\"" + container + "\", " + QString::number(rowIdx) + ", " + QString::number(MIN_ROW_COUNT) + ");");
    }
#endif
}

void TableWidget::addRow(const QString &dataId, const QStringList &ds) {
#if (QT_VERSION < 0x050400) //Qt 5.7
    QString row;
    row += "<tr class=\"filled-row\">";
    row += createRow(ds);
    row += "</tr>";
    QWebElement body = container.findFirst("tbody");
    QWebElement emptyRow = body.findFirst(".empty-row");
    if (emptyRow.isNull()) {
        body.appendInside(row);
        rows[dataId] = body.lastChild();
    } else {
        emptyRow.setOuterXml(row);
        rows[dataId] = body.findAll(".filled-row").last();
    }
#else
    dashboard->page()->runJavaScript("addRow(\"" + container + "\", \"" + Jsutils::toJSArray(ds) + "\", \"" + dataId + "\");");
    //assert(0);
#endif
}

#if (QT_VERSION < 0x050400) //Qt 5.7
void TableWidget::updateRow(const QString &dataId, const QStringList &d) {
    if (rows.contains(dataId)) {
        rows[dataId].setInnerXml(createRow(d));
    } else {
        addRow(dataId, d);
    }
}

QString TableWidget::createRow(const QStringList &ds) {
    QString row;
    foreach (const QString &d, ds) {
        row += "<td>" + d + "</td>";
    }
    return row;
}

QString TableWidget::wrapLongText(const QString &text) {
    return "<div class=\"long-text\" title=\"" + text + "\">" + text + "</div>";
}
#endif

QString Jsutils::toJSArray(const QStringList& list) {
    QString result;
    foreach(const QString& i, list) {
        result += "'" + i + "', ";
    }
    result.remove(result.length() - 2, 2);
    result = "[" + result + "]";
    return result;
}

QString Jsutils::toJSArray(const QList<int>& list) {
    QString result;
    foreach(int i, list) {
        result += "'" + QString::number(i) + "', ";
    }
    result.remove(result.length() - 2, 2);
    result = "[" + result + "]";
    return result;
}

} // U2
