/**
 * UGENE - Integrated Bioinformatics Tools.
 * Copyright (C) 2008-2012 UniPro <ugene@unipro.ru>
 * http://ugene.unipro.ru
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

#include <QVBoxLayout>
#include <QLabel>
#include <QSizePolicy>

#include <U2Core/AppContext.h>
#include <U2Core/U2OpStatusUtils.h>
#include <U2Core/U2SafePoints.h>

#include <U2Gui/MainWindow.h>

#include <U2Lang/WizardPage.h>
#include <U2Lang/WorkflowUtils.h>

#include "ElementSelectorController.h"
#include "PropertyWizardController.h"
#include "WDWizardPage.h"
#include "WizardPageController.h"

#include "WizardController.h"

namespace U2 {

WizardController::WizardController(Schema *s, Wizard *w)
: schema(s), wizard(w)
{
    broken = false;
    currentActors = s->getProcesses();
    vars = w->getVariables();
}

WizardController::~WizardController() {
    qDeleteAll(pageControllers);
}

QWizard * WizardController::createGui() {
    QWizard *result = new QWizard((QWidget*)AppContext::getMainWindow()->getQMainWindow());
    int idx = 0;
    foreach (WizardPage *page, wizard->getPages()) {
        result->setPage(idx, createPage(page));
        idMap[page->getId()] = idx;
        idx++;
    }
    result->setWizardStyle(QWizard::ClassicStyle);
    result->setModal(true);
    result->setAutoFillBackground(true);
    result->setWindowTitle(wizard->getName());
    return result;
}

void WizardController::assignParameters() {
    foreach (const QString &attrId, propValues.keys()) {
        Attribute *attr = getAttributeById(attrId);
        if (NULL == attr) {
            continue;
        }
        attr->setAttributeValue(propValues[attrId]);
    }
}

const QList<Actor*> & WizardController::getCurrentActors() const {
    return currentActors;
}

QVariant WizardController::getWidgetValue(AttributeWidget *widget) const {
    QString attrId;
    Attribute *attr = getAttribute(widget, attrId);
    CHECK(NULL != attr, QVariant());

    if (propValues.contains(attrId)) {
        return propValues[attrId];
    }
    return attr->getAttributePureValue();
}

void WizardController::setWidgetValue(AttributeWidget *widget, const QVariant &value) {
    QString attrId;
    Attribute *attr = getAttribute(widget, attrId);
    CHECK(NULL != attr, );

    propValues[attrId] = value;
}

Attribute * WizardController::getAttribute(AttributeWidget *widget, QString &attrId) const {
    U2OpStatusImpl os;
    widget->validate(currentActors, os);
    CHECK_OP(os, NULL);
    Actor *actor = WorkflowUtils::actorById(currentActors, widget->getActorId());
    Attribute *attr = actor->getParameter(widget->getAttributeId());

    attrId = getAttributeId(actor, attr);
    return attr;
}

QString WizardController::getAttributeId(Actor *actor, Attribute *attr) const {
    ActorPrototype *proto = actor->getProto();
    return proto->getId() + "." + actor->getId() + "." + attr->getId();
}

Attribute * WizardController::getAttributeById(const QString &attrId) const {
    QStringList tokens = attrId.split("."); // protoId.actorId.attrId
    CHECK(3 == tokens.size(), NULL);
    Actor *actor = WorkflowUtils::actorById(currentActors, tokens[1]);
    CHECK(NULL != actor, NULL);
    ActorPrototype *proto = actor->getProto();
    CHECK(tokens[0] == proto->getId(), NULL);

    return actor->getParameter(tokens[2]);
}

QWizardPage * WizardController::createPage(WizardPage *page) {
    WizardPageController *controller = new WizardPageController(this, page);
    WDWizardPage *result = new WDWizardPage(controller);

    pageControllers << controller;

    return result;
}

int WizardController::getQtId(const QString &hrId) const {
    return idMap[hrId];
}

const QMap<QString, Variable> & WizardController::getVariables() const {
    return vars;
}

QVariant WizardController::getSelectorValue(ElementSelectorWidget *widget) {
    SAFE_POINT(vars.contains(widget->getActorId()),
        QObject::tr("Undefined variable: %1").arg(widget->getActorId()), QVariant());
    Variable &v = vars[widget->getActorId()];
    if (v.isAssigned()) {
        return v.getValue();
    }

    // variable is not assigned yet => selector is not registered
    registerSelector(widget);
    SelectorValue sv = widget->getValues().first();
    v.setValue(sv.getValue());
    // now variable is assigned, selector is registered
    setSelectorValue(widget, sv.getValue());
    return sv.getValue();
}

void WizardController::setSelectorValue(ElementSelectorWidget *widget, const QVariant &value) {
    SAFE_POINT(vars.contains(widget->getActorId()),
        QObject::tr("Undefined variable: %1").arg(widget->getActorId()), );
    Variable &v = vars[widget->getActorId()];
    v.setValue(value.toString());
    replaceCurrentActor(widget->getActorId(), value.toString());
}

void WizardController::registerSelector(ElementSelectorWidget *widget) {
    if (selectors.contains(widget->getActorId())) {
        SAFE_POINT(false, QObject::tr("Actors selector is already defined: %1").arg(widget->getActorId()), );
    }
    U2OpStatusImpl os;
    SelectorActors actors(widget, currentActors, os);
    SAFE_POINT_OP(os, );
    selectors[widget->getActorId()] = actors;
}

void WizardController::replaceCurrentActor(const QString &actorId, const QString &selectorValue) {
    if (!selectors.contains(actorId)) {
        SAFE_POINT(false, QObject::tr("Unknown actors selector: %1").arg(actorId), );
    }
    Actor *currentA = WorkflowUtils::actorById(currentActors, actorId);
    SAFE_POINT(NULL != currentA, QObject::tr("Unknown actor id: %1").arg(actorId), );
    Actor *newA = selectors[actorId].getActor(selectorValue);
    SAFE_POINT(NULL != newA, QObject::tr("Unknown actors selector value id: %1").arg(selectorValue), );

    int idx = currentActors.indexOf(currentA);
    currentActors.replace(idx, newA);
}

void WizardController::setBroken() {
    broken = true;
}

bool WizardController::isBroken() const {
    return broken;
}

WizardController::ApplyResult WizardController::applyChanges(Metadata &meta) {
    assignParameters();
    if (selectors.isEmpty()) {
        return OK;
    }

    foreach (const QString &varName, selectors.keys()) {
        SAFE_POINT(vars.contains(varName),
            QObject::tr("Undefined variable: %1").arg(varName), ACTORS_REPLACED);
        Variable &v = vars[varName];
        SelectorActors &s = selectors[varName];
        Actor *newActor = s.getActor(v.getValue());
        Actor *oldActor = s.getSourceActor();
        if (newActor != oldActor) {
            schema->replaceProcess(oldActor, newActor, s.getMappings(v.getValue()));
            meta.replaceProcess(oldActor->getId(), newActor->getId(), s.getMappings(v.getValue()));
        }
    }
    return ACTORS_REPLACED;
}

/************************************************************************/
/* WidgetCreator */
/************************************************************************/
WidgetCreator::WidgetCreator(WizardController *_wc)
: wc(_wc), labelSize(0), result(NULL), layout(NULL)
{

}

WidgetCreator::WidgetCreator(WizardController *_wc, int _labelSize)
: wc(_wc), labelSize(_labelSize), result(NULL), layout(NULL)
{

}

void WidgetCreator::visit(AttributeWidget *aw) {
    QString type = aw->getProperty(AttributeWidgetHints::TYPE);
    PropertyWizardController *controller = NULL;

    if (AttributeWidgetHints::DEFAULT == type) {
        controller = new DefaultPropertyController(wc, aw, labelSize);
    } else if (AttributeWidgetHints::DATASETS == type) {
        controller = new InUrlDatasetsController(wc, aw);
    } else {
        SAFE_POINT(false, QString("Unknown widget type: %1").arg(type), );
    }

    controllers << controller;
    U2OpStatusImpl os;
    result = controller->createGUI(os);
}

void WidgetCreator::visit(WidgetsArea *wa) {
    result = new QWidget();
    layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    result->setLayout(layout);
    foreach (WizardWidget *w, wa->getWidgets()) {
        WidgetCreator wcr(wc, wa->getLabelSize());
        w->accept(&wcr);
        if (NULL != wcr.getResult()) {
            wcr.getResult()->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Maximum);
            layout->addWidget(wcr.getResult());
            controllers << wcr.getControllers();
        }
    }
    QSpacerItem *spacer = new QSpacerItem(0, 0, QSizePolicy::Maximum, QSizePolicy::Minimum);
    layout->addSpacerItem(spacer);
}

void WidgetCreator::visit(GroupWidget *gw) {
    visit((WidgetsArea*)gw);
    bool collapsible = (gw->getType() == GroupWidget::HIDEABLE);
    GroupBox *gb = new GroupBox(collapsible, gw->getTitle());
    setGroupBoxLayout(gb);
}

void WidgetCreator::visit(LogoWidget *lw) {
    result = new QWidget();
    layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    result->setLayout(layout);

    QLabel *label = new QLabel(result);
    QPixmap pix;
    if (lw->isDefault()) {
        pix = QPixmap(QString(":U2Designer/images/logo.png"));
    } else {
        pix = QPixmap(lw->getLogoPath());
    }
    pix = pix.scaled(191, 338, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    label->setPixmap(pix);
    label->setFixedSize(pix.size());
    layout->addWidget(label);
}

void WidgetCreator::visit(ElementSelectorWidget *esw) {
    ElementSelectorController *controller = new ElementSelectorController(wc, esw, labelSize);
    controllers << controller;
    U2OpStatusImpl os;
    result = controller->createGUI(os);
}

QWidget * WidgetCreator::getResult() {
    return result;
}

QList<WidgetController*> & WidgetCreator::getControllers() {
    return controllers;
}

QBoxLayout * WidgetCreator::getLayout() {
    return layout;
}

void WidgetCreator::setGroupBoxLayout(GroupBox *gb) {
    gb->setLayout(layout);
    result->setLayout(NULL);
    delete result;
    result = gb;
}

/************************************************************************/
/* PageContentCreator */
/************************************************************************/
PageContentCreator::PageContentCreator(WizardController *_wc)
: wc(_wc), result(NULL)
{

}

void PageContentCreator::visit(DefaultPageContent *content) {
    QHBoxLayout *layout = new QHBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    { // create logo
        WidgetCreator logoWC(wc);
        content->getLogoArea()->accept(&logoWC);
        if (NULL != logoWC.getResult()) {
            layout->addWidget(logoWC.getResult());
            controllers << logoWC.getControllers();
        }
    }
    { // create parameters
        WidgetCreator paramsWC(wc);
        content->getParamsArea()->accept(&paramsWC);
        if (NULL != paramsWC.getResult()) {
            if (NULL != paramsWC.getLayout()) {
                QSpacerItem *spacer = new QSpacerItem(0, 0, QSizePolicy::Maximum, QSizePolicy::MinimumExpanding);
                paramsWC.getLayout()->addSpacerItem(spacer);
            }
            layout->addWidget(paramsWC.getResult());
            controllers << paramsWC.getControllers();
        }
    }
    result = layout;
}

QLayout * PageContentCreator::getResult() {
    return result;
}

QList<WidgetController*> & PageContentCreator::getControllers() {
    return controllers;
}

/************************************************************************/
/* GroupBox */
/************************************************************************/
const int GroupBox::MARGIN = 5;

GroupBox::GroupBox(bool collapsible, const QString &title)
: QGroupBox(title), hLayout(NULL), tip(NULL), showHideButton(NULL)
{
    ui = new QWidget(this);
    ui->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Maximum);
    QVBoxLayout *layout = new QVBoxLayout();
    QGroupBox::setLayout(layout);
    layout->setContentsMargins(MARGIN, MARGIN, MARGIN, MARGIN);

    if (collapsible) {
        hLayout = new QHBoxLayout();
        tip = new QLabel(this);
        hLayout->addWidget(tip);
        hLayout->setContentsMargins(0, 0, 0, 0);
        showHideButton = new QToolButton(this);
        showHideButton->setFixedSize(19, 19);
        hLayout->addWidget(showHideButton);
        layout->addLayout(hLayout);
    }

    layout->addWidget(ui);

    if (collapsible) {
        sl_collapse();
        connect(showHideButton, SIGNAL(clicked()), this, SLOT(sl_onCheck()));
    }
}

void GroupBox::sl_collapse() {
    ui->hide();
    changeView("+", tr("Show"));
}

void GroupBox::sl_expand() {
    ui->show();
    changeView("-", tr("Hide"));
}

void GroupBox::setLayout(QLayout *l) {
    ui->setLayout(l);
}

void GroupBox::sl_onCheck() {
    if (ui->isVisible()) {
        sl_collapse();
    } else {
        sl_expand();
    }
}

void GroupBox::changeView(const QString &buttonText, const QString &showHide) {
    CHECK(NULL != showHideButton, );
    showHideButton->setText(buttonText);

    CHECK(NULL != tip, );
    QString parametersStr = tr("additional");
    if (!title().isEmpty()) {
        parametersStr = title().toLower();
    }
    tip->setText(showHide + " " + parametersStr + tr(" parameters"));
    showHideButton->setToolTip(tip->text());

    CHECK(NULL != hLayout, );
    if (!title().isEmpty()) {
        QMargins m = layout()->contentsMargins();
        m.setTop(0);
        layout()->setContentsMargins(m);
    }
}

} // U2
