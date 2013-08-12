/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "propertyeditorview.h"

#include <qmldesignerconstants.h>

#include <nodemetainfo.h>

#include <invalididexception.h>
#include <rewritingexception.h>
#include <variantproperty.h>

#include <bindingproperty.h>

#include <nodeabstractproperty.h>
#include <rewriterview.h>

#include "propertyeditorvalue.h"
#include "basiclayouts.h"
#include "basicwidgets.h"
#include "resetwidget.h"
#include "qlayoutobject.h"
#include <qmleditorwidgets/colorwidgets.h>
#include "gradientlineqmladaptor.h"
#include "behaviordialog.h"
#include "fontwidget.h"
#include "siblingcombobox.h"
#include "propertyeditortransaction.h"
#include "originwidget.h"

#include <utils/fileutils.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QDebug>
#include <QTimer>
#include <QShortcut>
#include <QMessageBox>
#include <QApplication>

enum {
    debug = false
};

const int collapseButtonOffset = 114;
namespace QmlDesigner {

PropertyEditorView::PropertyEditorView(QWidget *parent) :
        AbstractView(parent),
        m_parent(parent),
        m_updateShortcut(0),
        m_timerId(0),
        m_stackedWidget(new PropertyEditorWidget(parent)),
        m_qmlBackEndForCurrentType(0),
        m_locked(false),
        m_setupCompleted(false),
        m_singleShotTimer(new QTimer(this))
{
    m_updateShortcut = new QShortcut(QKeySequence("F3"), m_stackedWidget);
    connect(m_updateShortcut, SIGNAL(activated()), this, SLOT(reloadQml()));

    m_stackedWidget->setStyleSheet(
            QLatin1String(Utils::FileReader::fetchQrc(":/qmldesigner/stylesheet.css")));
    m_stackedWidget->setMinimumWidth(320);
    m_stackedWidget->move(0, 0);
    connect(m_stackedWidget, SIGNAL(resized()), this, SLOT(updateSize()));

    m_stackedWidget->insertWidget(0, new QWidget(m_stackedWidget));


    static bool declarativeTypesRegistered = false;
    if (!declarativeTypesRegistered) {
        declarativeTypesRegistered = true;
        BasicWidgets::registerDeclarativeTypes();
        BasicLayouts::registerDeclarativeTypes();
        ResetWidget::registerDeclarativeType();
        QLayoutObject::registerDeclarativeType();
        QmlEditorWidgets::ColorWidgets::registerDeclarativeTypes();
        BehaviorDialog::registerDeclarativeType();
        PropertyEditorValue::registerDeclarativeTypes();
        FontWidget::registerDeclarativeTypes();
        SiblingComboBox::registerDeclarativeTypes();
        OriginWidget::registerDeclarativeType();
        GradientLineQmlAdaptor::registerDeclarativeType();
    }
    setQmlDir(PropertyEditorQmlBackend::propertyEditorResourcesPath());
    m_stackedWidget->setWindowTitle(tr("Properties"));
}

PropertyEditorView::~PropertyEditorView()
{
    qDeleteAll(m_qmlBackendHash);
}

void PropertyEditorView::setupPane(const TypeName &typeName)
{
    NodeMetaInfo metaInfo = model()->metaInfo(typeName);

    QUrl qmlFile = PropertyEditorQmlBackend::fileToUrl(
                PropertyEditorQmlBackend::locateQmlFile(metaInfo, QLatin1String("Qt/ItemPane.qml")));
    QUrl qmlSpecificsFile;

    qmlSpecificsFile = PropertyEditorQmlBackend::fileToUrl(PropertyEditorQmlBackend::locateQmlFile(
                                                               metaInfo, PropertyEditorQmlBackend::fixTypeNameForPanes(typeName)
                                                               + "Specifics.qml"));

    PropertyEditorQmlBackend *qmlBackend = m_qmlBackendHash.value(qmlFile.toString());

    if (!qmlBackend) {
        qmlBackend = new PropertyEditorQmlBackend(this);

        qmlBackend->context()->setContextProperty("finishedNotify", QVariant(false) );
        qmlBackend->initialSetup(typeName, qmlSpecificsFile, this);
        qmlBackend->setSource(qmlFile);
        qmlBackend->context()->setContextProperty("finishedNotify", QVariant(true) );

        m_stackedWidget->addWidget(qmlBackend->widget());
        m_qmlBackendHash.insert(qmlFile.toString(), qmlBackend);
    } else {
        qmlBackend->context()->setContextProperty("finishedNotify", QVariant(false) );

        qmlBackend->initialSetup(typeName, qmlSpecificsFile, this);
        qmlBackend->context()->setContextProperty("finishedNotify", QVariant(true) );
    }
}

void PropertyEditorView::changeValue(const QString &name)
{
    PropertyName propertyName = name.toUtf8();

    if (propertyName.isNull())
        return;

    if (m_locked)
        return;

    if (propertyName == "type")
        return;

    if (!m_selectedNode.isValid())
        return;

    if (propertyName == "id") {
        PropertyEditorValue *value = m_qmlBackEndForCurrentType->propertyValueForName(propertyName);
        const QString newId = value->value().toString();

        if (newId == m_selectedNode.id())
            return;

        if (m_selectedNode.isValidId(newId)  && !modelNodeForId(newId).isValid() ) {
            if (m_selectedNode.id().isEmpty() || newId.isEmpty()) { //no id
                try {
                    m_selectedNode.setId(newId);
                } catch (InvalidIdException &e) { //better save then sorry
                    m_locked = true;
                    value->setValue(m_selectedNode.id());
                    m_locked = false;
                    QMessageBox::warning(0, tr("Invalid Id"), e.description());
                }
            } else { //there is already an id, so we refactor
                if (rewriterView())
                    rewriterView()->renameId(m_selectedNode.id(), newId);
            }
        } else {
            m_locked = true;
            value->setValue(m_selectedNode.id());
            m_locked = false;
            if (!m_selectedNode.isValidId(newId))
                QMessageBox::warning(0, tr("Invalid Id"),  tr("%1 is an invalid id").arg(newId));
            else
                QMessageBox::warning(0, tr("Invalid Id"),  tr("%1 already exists").arg(newId));
        }
        return;
    }

    //.replace(QLatin1Char('.'), QLatin1Char('_'))
    PropertyName underscoreName(propertyName);
    underscoreName.replace('.', '_');
    PropertyEditorValue *value = m_qmlBackEndForCurrentType->propertyValueForName(underscoreName);

    if (value ==0)
        return;

    QmlObjectNode qmlObjectNode(m_selectedNode);

    QVariant castedValue;

    if (qmlObjectNode.modelNode().metaInfo().isValid() && qmlObjectNode.modelNode().metaInfo().hasProperty(propertyName)) {
        castedValue = qmlObjectNode.modelNode().metaInfo().propertyCastedValue(propertyName, value->value());
    } else {
        qWarning() << "PropertyEditor:" <<propertyName << "cannot be casted (metainfo)";
        return ;
    }

    if (value->value().isValid() && !castedValue.isValid()) {
        qWarning() << "PropertyEditor:" << propertyName << "not properly casted (metainfo)";
        return ;
    }

    if (qmlObjectNode.modelNode().metaInfo().isValid() && qmlObjectNode.modelNode().metaInfo().hasProperty(propertyName))
        if (qmlObjectNode.modelNode().metaInfo().propertyTypeName(propertyName) == "QUrl"
                || qmlObjectNode.modelNode().metaInfo().propertyTypeName(propertyName) == "url") { //turn absolute local file paths into relative paths
            QString filePath = castedValue.toUrl().toString();
        if (QFileInfo(filePath).exists() && QFileInfo(filePath).isAbsolute()) {
            QDir fileDir(QFileInfo(model()->fileUrl().toLocalFile()).absolutePath());
            castedValue = QUrl(fileDir.relativeFilePath(filePath));
        }
    }

        if (castedValue.type() == QVariant::Color) {
            QColor color = castedValue.value<QColor>();
            QColor newColor = QColor(color.name());
            newColor.setAlpha(color.alpha());
            castedValue = QVariant(newColor);
        }

        try {
            if (!value->value().isValid()) { //reset
                qmlObjectNode.removeProperty(propertyName);
            } else {
                if (castedValue.isValid() && !castedValue.isNull()) {
                    m_locked = true;
                    qmlObjectNode.setVariantProperty(propertyName, castedValue);
                    m_locked = false;
                }
            }
        }
        catch (RewritingException &e) {
            QMessageBox::warning(0, "Error", e.description());
        }
}

void PropertyEditorView::changeExpression(const QString &propertyName)
{
    PropertyName name = propertyName.toUtf8();

    if (name.isNull())
        return;

    if (m_locked)
        return;

    RewriterTransaction transaction = beginRewriterTransaction();

    try {
        PropertyName underscoreName(name);
        underscoreName.replace('.', '_');

        QmlObjectNode qmlObjectNode(m_selectedNode);
        PropertyEditorValue *value = m_qmlBackEndForCurrentType->propertyValueForName(underscoreName);

        if (qmlObjectNode.modelNode().metaInfo().isValid() && qmlObjectNode.modelNode().metaInfo().hasProperty(name)) {
            if (qmlObjectNode.modelNode().metaInfo().propertyTypeName(name) == "QColor") {
                if (QColor(value->expression().remove('"')).isValid()) {
                    qmlObjectNode.setVariantProperty(name, QColor(value->expression().remove('"')));
                    transaction.commit(); //committing in the try block
                    return;
                }
            } else if (qmlObjectNode.modelNode().metaInfo().propertyTypeName(name) == "bool") {
                if (value->expression().compare("false", Qt::CaseInsensitive) == 0 || value->expression().compare("true", Qt::CaseInsensitive) == 0) {
                    if (value->expression().compare("true", Qt::CaseInsensitive) == 0)
                        qmlObjectNode.setVariantProperty(name, true);
                    else
                        qmlObjectNode.setVariantProperty(name, false);
                    transaction.commit(); //committing in the try block
                    return;
                }
            } else if (qmlObjectNode.modelNode().metaInfo().propertyTypeName(name) == "int") {
                bool ok;
                int intValue = value->expression().toInt(&ok);
                if (ok) {
                    qmlObjectNode.setVariantProperty(name, intValue);
                    transaction.commit(); //committing in the try block
                    return;
                }
            } else if (qmlObjectNode.modelNode().metaInfo().propertyTypeName(name) == "qreal") {
                bool ok;
                qreal realValue = value->expression().toFloat(&ok);
                if (ok) {
                    qmlObjectNode.setVariantProperty(name, realValue);
                    transaction.commit(); //committing in the try block
                    return;
                }
            }
        }

        if (!value) {
            qWarning() << "PropertyEditor::changeExpression no value for " << underscoreName;
            return;
        }

        if (value->expression().isEmpty())
            return;

        if (qmlObjectNode.expression(name) != value->expression() || !qmlObjectNode.propertyAffectedByCurrentState(name))
            qmlObjectNode.setBindingProperty(name, value->expression());

        transaction.commit(); //committing in the try block
    }

    catch (RewritingException &e) {
        QMessageBox::warning(0, "Error", e.description());
    }
}

void PropertyEditorView::updateSize()
{
    if (!m_qmlBackEndForCurrentType)
        return;
    QWidget* frame = m_qmlBackEndForCurrentType->widget()->findChild<QWidget*>("propertyEditorFrame");
    if (frame)
        frame->resize(m_stackedWidget->size());
}

void PropertyEditorView::setupPanes()
{
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    setupPane("QtQuick.Item");
    resetView();
    m_setupCompleted = true;
    QApplication::restoreOverrideCursor();
}

void PropertyEditorView::setQmlDir(const QString &qmlDir)
{
    m_qmlDir = qmlDir;


    QFileSystemWatcher *watcher = new QFileSystemWatcher(this);
    watcher->addPath(m_qmlDir);
    connect(watcher, SIGNAL(directoryChanged(QString)), this, SLOT(reloadQml()));
}

void PropertyEditorView::delayedResetView()
{
    if (m_timerId == 0)
        m_timerId = startTimer(100);
}

void PropertyEditorView::timerEvent(QTimerEvent *timerEvent)
{
    if (m_timerId == timerEvent->timerId())
        resetView();
}

void PropertyEditorView::resetView()
{
    if (model() == 0)
        return;

    m_locked = true;

    if (debug)
        qDebug() << "________________ RELOADING PROPERTY EDITOR QML _______________________";

    if (m_timerId)
        killTimer(m_timerId);

    if (m_selectedNode.isValid() && model() != m_selectedNode.model())
        m_selectedNode = ModelNode();

    TypeName specificsClassName;
    QUrl qmlFile(PropertyEditorQmlBackend::qmlForNode(m_selectedNode, specificsClassName));
    QUrl qmlSpecificsFile;

    TypeName diffClassName;
    if (m_selectedNode.isValid()) {
        diffClassName = m_selectedNode.metaInfo().typeName();
        QList<NodeMetaInfo> hierarchy;
        hierarchy << m_selectedNode.metaInfo();
        hierarchy << m_selectedNode.metaInfo().superClasses();

        foreach (const NodeMetaInfo &info, hierarchy) {
            if (QFileInfo(PropertyEditorQmlBackend::fileFromUrl(qmlSpecificsFile)).exists())
                break;
            qmlSpecificsFile = PropertyEditorQmlBackend::fileToUrl(PropertyEditorQmlBackend::locateQmlFile(
                                                                       info,
                                                                       PropertyEditorQmlBackend::fixTypeNameForPanes(info.typeName())
                                                                       + "Specifics.qml"));
            diffClassName = info.typeName();
        }
    }

    if (!QFileInfo(PropertyEditorQmlBackend::fileFromUrl(qmlSpecificsFile)).exists())
        diffClassName = specificsClassName;

    QString specificQmlData;

    if (m_selectedNode.isValid() && m_selectedNode.metaInfo().isValid() && diffClassName != m_selectedNode.type()) {
        //do magic !!
        specificQmlData = PropertyEditorQmlBackend::templateGeneration(m_selectedNode.metaInfo(), model()->metaInfo(diffClassName), m_selectedNode);
    }

    PropertyEditorQmlBackend *qmlBackend = m_qmlBackendHash.value(qmlFile.toString());

    QString currentStateName = currentState().isBaseState() ? currentState().name() : QLatin1String("invalid state");

    if (!qmlBackend) {
        qmlBackend = new PropertyEditorQmlBackend(this);

        m_stackedWidget->addWidget(qmlBackend->widget());
        m_qmlBackendHash.insert(qmlFile.toString(), qmlBackend);

        QmlObjectNode qmlObjectNode;
        if (m_selectedNode.isValid()) {
            qmlObjectNode = QmlObjectNode(m_selectedNode);
            Q_ASSERT(qmlObjectNode.isValid());
        }
        qmlBackend->setup(qmlObjectNode, currentStateName, qmlSpecificsFile, this);
        qmlBackend->context()->setContextProperty("finishedNotify", QVariant(false));
        if (specificQmlData.isEmpty())
            qmlBackend->contextObject()->setSpecificQmlData(specificQmlData);

        qmlBackend->contextObject()->setGlobalBaseUrl(qmlFile);
        qmlBackend->contextObject()->setSpecificQmlData(specificQmlData);
        qmlBackend->setSource(qmlFile);
        qmlBackend->context()->setContextProperty("finishedNotify", QVariant(true));
    } else {
        QmlObjectNode qmlObjectNode;
        if (m_selectedNode.isValid())
            qmlObjectNode = QmlObjectNode(m_selectedNode);

        qmlBackend->context()->setContextProperty("finishedNotify", QVariant(false));
        if (specificQmlData.isEmpty())
            qmlBackend->contextObject()->setSpecificQmlData(specificQmlData);
        qmlBackend->setup(qmlObjectNode, currentStateName, qmlSpecificsFile, this);
        qmlBackend->contextObject()->setGlobalBaseUrl(qmlFile);
        qmlBackend->contextObject()->setSpecificQmlData(specificQmlData);
    }

    m_stackedWidget->setCurrentWidget(qmlBackend->widget());

    qmlBackend->context()->setContextProperty("finishedNotify", QVariant(true));

    qmlBackend->contextObject()->triggerSelectionChanged();

    m_qmlBackEndForCurrentType = qmlBackend;

    m_locked = false;

    if (m_timerId)
        m_timerId = 0;

    updateSize();
}

void PropertyEditorView::selectedNodesChanged(const QList<ModelNode> &selectedNodeList,
                                          const QList<ModelNode> &lastSelectedNodeList)
{
    Q_UNUSED(lastSelectedNodeList);

    if (selectedNodeList.isEmpty() || selectedNodeList.count() > 1)
        select(ModelNode());
    else if (m_selectedNode != selectedNodeList.first())
        select(selectedNodeList.first());
}

void PropertyEditorView::nodeAboutToBeRemoved(const ModelNode &removedNode)
{
    if (m_selectedNode.isValid() && removedNode.isValid() && m_selectedNode == removedNode)
        select(m_selectedNode.parentProperty().parentModelNode());
}

void PropertyEditorView::modelAttached(Model *model)
{
    AbstractView::modelAttached(model);

    if (debug)
        qDebug() << Q_FUNC_INFO;

    m_locked = true;

    resetView();
    if (!m_setupCompleted) {
        m_singleShotTimer->setSingleShot(true);
        m_singleShotTimer->setInterval(100);
        connect(m_singleShotTimer, SIGNAL(timeout()), this, SLOT(setupPanes()));
        m_singleShotTimer->start();
    }

    m_locked = false;
}

void PropertyEditorView::modelAboutToBeDetached(Model *model)
{
    AbstractView::modelAboutToBeDetached(model);
    m_qmlBackEndForCurrentType->propertyEditorTransaction()->end();

    resetView();
}

void PropertyEditorView::propertiesRemoved(const QList<AbstractProperty>& propertyList)
{
    if (!m_selectedNode.isValid())
        return;

    if (!QmlObjectNode(m_selectedNode).isValid())
        return;

    foreach (const AbstractProperty &property, propertyList) {
        ModelNode node(property.parentModelNode());
        if (node == m_selectedNode || QmlObjectNode(m_selectedNode).propertyChangeForCurrentState() == node) {
            setValue(m_selectedNode, property.name(), QmlObjectNode(m_selectedNode).instanceValue(property.name()));
            if (property.name().contains("anchor"))
                m_qmlBackEndForCurrentType->backendAnchorBinding().invalidate(m_selectedNode);
        }
    }
}

void PropertyEditorView::variantPropertiesChanged(const QList<VariantProperty>& propertyList, PropertyChangeFlags /*propertyChange*/)
{

    if (!m_selectedNode.isValid())
        return;

    if (!QmlObjectNode(m_selectedNode).isValid())
        return;

    foreach (const VariantProperty &property, propertyList) {
        ModelNode node(property.parentModelNode());

        if (node == m_selectedNode || QmlObjectNode(m_selectedNode).propertyChangeForCurrentState() == node) {
            if ( QmlObjectNode(m_selectedNode).modelNode().property(property.name()).isBindingProperty())
                setValue(m_selectedNode, property.name(), QmlObjectNode(m_selectedNode).instanceValue(property.name()));
            else
                setValue(m_selectedNode, property.name(), QmlObjectNode(m_selectedNode).modelValue(property.name()));
        }
    }
}

void PropertyEditorView::bindingPropertiesChanged(const QList<BindingProperty>& propertyList, PropertyChangeFlags /*propertyChange*/)
{
    if (!m_selectedNode.isValid())
        return;

       if (!QmlObjectNode(m_selectedNode).isValid())
        return;

    foreach (const BindingProperty &property, propertyList) {
        ModelNode node(property.parentModelNode());

        if (node == m_selectedNode || QmlObjectNode(m_selectedNode).propertyChangeForCurrentState() == node) {
            if (property.name().contains("anchor"))
                m_qmlBackEndForCurrentType->backendAnchorBinding().invalidate(m_selectedNode);
            if ( QmlObjectNode(m_selectedNode).modelNode().property(property.name()).isBindingProperty())
                setValue(m_selectedNode, property.name(), QmlObjectNode(m_selectedNode).instanceValue(property.name()));
            else
                setValue(m_selectedNode, property.name(), QmlObjectNode(m_selectedNode).modelValue(property.name()));
        }
    }
}

void PropertyEditorView::signalHandlerPropertiesChanged(const QVector<SignalHandlerProperty> & /*propertyList*/,
                                                    AbstractView::PropertyChangeFlags /*propertyChange*/)
{
}

void PropertyEditorView::instanceInformationsChange(const QMultiHash<ModelNode, InformationName> &informationChangeHash)
{
    if (!m_selectedNode.isValid())
        return;

    m_locked = true;
    QList<InformationName> informationNameList = informationChangeHash.values(m_selectedNode);
    if (informationNameList.contains(Anchor)
            || informationNameList.contains(HasAnchor))
        m_qmlBackEndForCurrentType->backendAnchorBinding().setup(QmlItemNode(m_selectedNode));
    m_locked = false;
}

void PropertyEditorView::nodeIdChanged(const ModelNode& node, const QString& newId, const QString& /*oldId*/)
{
    if (!m_selectedNode.isValid())
        return;

    if (!QmlObjectNode(m_selectedNode).isValid())
        return;

    if (node == m_selectedNode) {

        if (m_qmlBackEndForCurrentType)
            setValue(node, "id", newId);
    }
}

void PropertyEditorView::scriptFunctionsChanged(const ModelNode &/*node*/, const QStringList &/*scriptFunctionList*/)
{
}

void PropertyEditorView::select(const ModelNode &node)
{
    if (QmlObjectNode(node).isValid())
        m_selectedNode = node;
    else
        m_selectedNode = ModelNode();

    delayedResetView();
}

bool PropertyEditorView::hasWidget() const
{
    return true;
}

WidgetInfo PropertyEditorView::widgetInfo()
{
    return createWidgetInfo(m_stackedWidget, 0, QLatin1String("Properties"), WidgetInfo::RightPane, 0);
}

void PropertyEditorView::currentStateChanged(const ModelNode &node)
{
    QmlModelState newQmlModelState(node);
    Q_ASSERT(newQmlModelState.isValid());
    if (debug)
        qDebug() << Q_FUNC_INFO << newQmlModelState.name();
    delayedResetView();
}

void PropertyEditorView::instancePropertyChange(const QList<QPair<ModelNode, PropertyName> > &propertyList)
{
    if (!m_selectedNode.isValid())
        return;
    m_locked = true;

    typedef QPair<ModelNode, PropertyName> ModelNodePropertyPair;
    foreach (const ModelNodePropertyPair &propertyPair, propertyList) {
        const ModelNode modelNode = propertyPair.first;
        const QmlObjectNode qmlObjectNode(modelNode);
        const PropertyName propertyName = propertyPair.second;

        if (qmlObjectNode.isValid() && m_qmlBackEndForCurrentType && modelNode == m_selectedNode && qmlObjectNode.currentState().isValid()) {
            const AbstractProperty property = modelNode.property(propertyName);
            if (modelNode == m_selectedNode || qmlObjectNode.propertyChangeForCurrentState() == qmlObjectNode) {
                if ( !modelNode.hasProperty(propertyName) || modelNode.property(property.name()).isBindingProperty() )
                    setValue(modelNode, property.name(), qmlObjectNode.instanceValue(property.name()));
                else
                    setValue(modelNode, property.name(), qmlObjectNode.modelValue(property.name()));
            }
        }

    }

    m_locked = false;

}

void PropertyEditorView::nodeCreated(const ModelNode &/*createdNode*/)
{

}

void PropertyEditorView::nodeRemoved(const ModelNode &/*removedNode*/, const NodeAbstractProperty &/*parentProperty*/, AbstractView::PropertyChangeFlags /*propertyChange*/)
{

}

void PropertyEditorView::nodeAboutToBeReparented(const ModelNode &/*node*/, const NodeAbstractProperty &/*newPropertyParent*/, const NodeAbstractProperty &/*oldPropertyParent*/, AbstractView::PropertyChangeFlags /*propertyChange*/)
{

}

void PropertyEditorView::nodeReparented(const ModelNode &/*node*/, const NodeAbstractProperty &/*newPropertyParent*/, const NodeAbstractProperty &/*oldPropertyParent*/, AbstractView::PropertyChangeFlags /*propertyChange*/)
{

}

void PropertyEditorView::propertiesAboutToBeRemoved(const QList<AbstractProperty> &/*propertyList*/)
{

}

void PropertyEditorView::rootNodeTypeChanged(const QString &/*type*/, int /*majorVersion*/, int /*minorVersion*/)
{
    // TODO: we should react to this case
}

void PropertyEditorView::instancesCompleted(const QVector<ModelNode> &/*completedNodeList*/)
{

}

void PropertyEditorView::instancesRenderImageChanged(const QVector<ModelNode> &/*nodeList*/)
{

}

void PropertyEditorView::instancesPreviewImageChanged(const QVector<ModelNode> &/*nodeList*/)
{

}

void PropertyEditorView::instancesChildrenChanged(const QVector<ModelNode> &/*nodeList*/)
{

}

void PropertyEditorView::instancesToken(const QString &/*tokenName*/, int /*tokenNumber*/, const QVector<ModelNode> &/*nodeVector*/)
{

}

void PropertyEditorView::nodeSourceChanged(const ModelNode &/*modelNode*/, const QString &/*newNodeSource*/)
{

}

void PropertyEditorView::rewriterBeginTransaction()
{

}

void PropertyEditorView::rewriterEndTransaction()
{

}

void PropertyEditorView::nodeOrderChanged(const NodeListProperty &/*listProperty*/, const ModelNode &/*movedNode*/, int /*oldIndex*/)
{

}

void PropertyEditorView::importsChanged(const QList<Import> &/*addedImports*/, const QList<Import> &/*removedImports*/)
{

}

void PropertyEditorView::setValue(const QmlObjectNode &qmlObjectNode, const PropertyName &name, const QVariant &value)
{
    m_locked = true;
    m_qmlBackEndForCurrentType->setValue(qmlObjectNode, name, value);
    m_locked = false;
}

void PropertyEditorView::reloadQml()
{
    m_qmlBackendHash.clear();
    while (QWidget *widget = m_stackedWidget->widget(0)) {
        m_stackedWidget->removeWidget(widget);
        delete widget;
    }
    m_qmlBackEndForCurrentType = 0;

    delayedResetView();
}


} //QmlDesigner

