/* $Id$ */
/** @file
 * VBox Qt GUI - UIGlobalToolsManagerWidget class implementation.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/* Qt includes: */
#include <QGridLayout>

/* GUI includes: */
#include "QIToolBar.h"
#include "UIChooser.h"
#include "UICommon.h"
#include "UIExtraDataManager.h"
#include "UIGlobalToolsManagerWidget.h"
#include "UIMachineToolsManagerWidget.h"
#include "UIToolPaneGlobal.h"
#include "UIToolPaneMachine.h"
#include "UITools.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualBoxManagerAdvancedWidget.h"
#include "UIVirtualMachineItem.h"

/* Other VBox includes: */
#include "iprt/assert.h"


UIGlobalToolsManagerWidget::UIGlobalToolsManagerWidget(UIVirtualBoxManagerAdvancedWidget *pParent,
                                                       UIActionPool *pActionPool)
    : QWidget(pParent)
    , m_pActionPool(pActionPool)
    , m_pLayout(0)
    , m_pMenu(0)
    , m_pPane(0)
{
    prepare();
}

void UIGlobalToolsManagerWidget::addToolBar(QIToolBar *pToolBar)
{
    AssertPtrReturnVoid(m_pLayout);
    m_pLayout->addWidget(pToolBar, 0, 1);
}

UIToolPaneGlobal *UIGlobalToolsManagerWidget::toolPane() const
{
    return m_pPane;
}

UIMachineToolsManagerWidget *UIGlobalToolsManagerWidget::machineToolManager() const
{
    return toolPane()->machineToolManager();
}

UIToolType UIGlobalToolsManagerWidget::menuToolType() const
{
    AssertPtrReturn(toolMenu(), UIToolType_Invalid);
    return toolMenu()->toolsType(UIToolClass_Global);
}

void UIGlobalToolsManagerWidget::setMenuToolType(UIToolType enmType)
{
    /* Sanity check: */
    AssertReturnVoid(enmType != UIToolType_Invalid);

    AssertPtrReturnVoid(toolMenu());
    toolMenu()->setToolsType(enmType);
}

UIToolType UIGlobalToolsManagerWidget::toolType() const
{
    AssertPtrReturn(toolPane(), UIToolType_Invalid);
    return toolPane()->currentTool();
}

bool UIGlobalToolsManagerWidget::isToolOpened(UIToolType enmType) const
{
    /* Sanity check: */
    AssertReturn(enmType != UIToolType_Invalid, false);
    /* Make sure new tool type is of Global class: */
    AssertReturn(UIToolStuff::isTypeOfClass(enmType, UIToolClass_Global), false);

    AssertPtrReturn(toolPane(), false);
    return toolPane()->isToolOpened(enmType);
}

void UIGlobalToolsManagerWidget::switchToolTo(UIToolType enmType)
{
    /* Sanity check: */
    AssertReturnVoid(enmType != UIToolType_Invalid);
    /* Make sure new tool type is of Global class: */
    AssertReturnVoid(UIToolStuff::isTypeOfClass(enmType, UIToolClass_Global));

    /* Open corresponding tool: */
    AssertPtrReturnVoid(toolPane());
    toolPane()->openTool(enmType);

    /* Special handling for Machines global tool,
     * notify Machine tool-pane it's active: */
    if (enmType == UIToolType_Machines)
    {
        toolPane()->setActive(false);
        toolPaneMachine()->setActive(true);
    }
    /* Otherwise, notify Global tool-pane it's active: */
    else
    {
        toolPaneMachine()->setActive(false);
        toolPane()->setActive(true);
    }

    /* Special handling for Activities Global tool,
     * start unconditionally updating all cloud VMs: */
    if (enmType == UIToolType_Activities)
    {
        chooser()->setKeepCloudNodesUpdated(true);
        toolPane()->setCloudMachineItems(chooser()->cloudMachineItems());
    }
    /* Otherwise, stop unconditionally updating all cloud VMs,
     * (tho they will still be updated if selected) */
    else
        chooser()->setKeepCloudNodesUpdated(false);

    /* Let the parent know: */
    emit sigToolTypeChange();
}

void UIGlobalToolsManagerWidget::closeTool(UIToolType enmType)
{
    /* Sanity check: */
    AssertReturnVoid(enmType != UIToolType_Invalid);
    /* Make sure new tool type is of Global class: */
    AssertReturnVoid(UIToolStuff::isTypeOfClass(enmType, UIToolClass_Global));

    AssertPtrReturnVoid(toolPane());
    toolPane()->closeTool(enmType);
}

QString UIGlobalToolsManagerWidget::currentHelpKeyword() const
{
    if (toolType() == UIToolType_Machines)
    {
        AssertPtrReturn(machineToolManager(), QString());
        return machineToolManager()->currentHelpKeyword();
    }

    AssertPtrReturn(toolPane(), QString());
    return toolPane()->currentHelpKeyword();
}

void UIGlobalToolsManagerWidget::sltHandleCommitData()
{
    cleanupConnections();
}

void UIGlobalToolsManagerWidget::sltHandleMachineRegistrationChanged(const QUuid &, const bool fRegistered)
{
    /* On any VM registered switch from Home to Machines: */
    AssertPtrReturnVoid(toolMenu());
    if (fRegistered && toolMenu()->toolsType(UIToolClass_Global) == UIToolType_Home)
        setMenuToolType(UIToolType_Machines);
}

void UIGlobalToolsManagerWidget::sltHandleSettingsExpertModeChange()
{
    /* Update tools restrictions: */
    emit sigToolMenuUpdate();
}

void UIGlobalToolsManagerWidget::sltHandleChooserPaneSelectionChange()
{
    /* Update tools restrictions: */
    emit sigToolMenuUpdate();
}

void UIGlobalToolsManagerWidget::sltHandleCloudProfileStateChange(const QString &, const QString &)
{
    /* If Global Activities tool is currently chosen: */
    AssertPtrReturnVoid(toolPane());
    if (toolType() == UIToolType_Activities)
    {
        /* Propagate a set of cloud machine items to Global tool-pane: */
        toolPane()->setCloudMachineItems(chooser()->cloudMachineItems());
    }
}

void UIGlobalToolsManagerWidget::sltHandleGlobalToolMenuUpdate()
{
    /* Prepare tool restrictions: */
    QSet<UIToolType> restrictedTypes;

    /* Restrict some types for Basic mode: */
    const bool fExpertMode = gEDataManager->isSettingsInExpertMode();
    if (!fExpertMode)
        restrictedTypes << UIToolType_Media
                        << UIToolType_Network;

    /* Make sure Machines tool is hidden for empty Chooser-pane: */
    if (!chooser()->currentItem())
        restrictedTypes << UIToolType_Machines;

    /* Make sure no restricted tool is selected: */
    if (restrictedTypes.contains(toolMenu()->toolsType(UIToolClass_Global)))
        setMenuToolType(UIToolType_Home);

    /* Hide restricted tools in the menu: */
    const QList restrictions(restrictedTypes.begin(), restrictedTypes.end());
    toolMenu()->setRestrictedToolTypes(UIToolClass_Global, restrictions);

    /* Close all restricted tools (besides the Machines): */
    foreach (const UIToolType &enmRestrictedType, restrictedTypes)
        if (enmRestrictedType != UIToolType_Machines)
            toolPane()->closeTool(enmRestrictedType);
}

void UIGlobalToolsManagerWidget::sltHandleMachineToolMenuUpdate(UIVirtualMachineItem *pItem)
{
    /* Prepare tool restrictions: */
    QSet<UIToolType> restrictedTypes;

    /* Restrict some types for Basic mode: */
    const bool fExpertMode = gEDataManager->isSettingsInExpertMode();
    if (!fExpertMode)
        restrictedTypes << UIToolType_FileManager;

    /* Make sure local VM tools are hidden for cloud VMs: */
    if (pItem && pItem->itemType() != UIVirtualMachineItemType_Local)
        restrictedTypes << UIToolType_Snapshots
                        << UIToolType_Logs
                        << UIToolType_FileManager;

    /* Make sure no restricted tool is selected: */
    if (restrictedTypes.contains(toolMenu()->toolsType(UIToolClass_Machine)))
        setMenuToolType(UIToolType_Details);

    /* Hide restricted tools in the menu: */
    const QList restrictions(restrictedTypes.begin(), restrictedTypes.end());
    toolMenu()->setRestrictedToolTypes(UIToolClass_Machine, restrictions);

    // /* Disable even unrestricted tools for inacccessible VMs: */
    // const bool fCurrentItemIsOk = isItemAccessible(pItem);
    // toolMenu()->setItemsEnabled(fCurrentItemIsOk);
}

void UIGlobalToolsManagerWidget::sltHandleToolsMenuIndexChange(UIToolType enmType)
{
    /* Determine tool class of passed tool type: */
    const UIToolClass enmClass = UIToolStuff::castTypeToClass(enmType);

    /* For Global tool class: */
    if (enmClass == UIToolClass_Global)
    {
        /* Mark Machine & Management tools [un]suitable depending on Global tool selected: */
        toolMenu()->setUnsuitableToolClass(UIToolClass_Machine, enmType != UIToolType_Machines);
        toolMenu()->setUnsuitableToolClass(UIToolClass_Management, enmType != UIToolType_Managers);

        /* Switch tool-pane accordingly: */
        switchToolTo(enmType);
    }
    /* For Machine tool class => switch tool-pane accordingly: */
    else if (enmClass == UIToolClass_Machine)
        machineToolManager()->switchToolTo(enmType);
    /* For Management tool class => switch tool-pane accordingly: */
    else if (enmClass == UIToolClass_Management)
        switchToolTo(enmType);
}

void UIGlobalToolsManagerWidget::sltSwitchToActivitiesTool()
{
    setMenuToolType(UIToolType_Activities);
}

void UIGlobalToolsManagerWidget::prepare()
{
    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Load settings: */
    loadSettings();
}

void UIGlobalToolsManagerWidget::prepareWidgets()
{
    /* Create layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        /* Configure layout: */
        m_pLayout->setContentsMargins(0, 0, 0, 0);
        m_pLayout->setSpacing(0);

        /* Create tool-menu: */
        m_pMenu = new UITools(this, UIToolClass_Global, actionPool(), Qt::Widget);
        if (toolMenu())
        {
            /* Add into layout: */
            m_pLayout->addWidget(toolMenu(), 0, 0, 2, 1);
        }

        /* Create tool-pane: */
        m_pPane = new UIToolPaneGlobal(actionPool());
        if (toolPane())
        {
            /// @todo make sure it's used properly
            toolPane()->setActive(true);

            /* Add into layout: */
            m_pLayout->addWidget(toolPane(), 1, 1);
        }
    }
}

void UIGlobalToolsManagerWidget::prepareConnections()
{
    /* UICommon connections: */
    connect(&uiCommon(), &UICommon::sigAskToCommitData,
            this, &UIGlobalToolsManagerWidget::sltHandleCommitData);

    /* Global COM event handlers: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineRegistered,
            this, &UIGlobalToolsManagerWidget::sltHandleMachineRegistrationChanged);
    connect(gEDataManager, &UIExtraDataManager::sigSettingsExpertModeChange,
            this, &UIGlobalToolsManagerWidget::sltHandleSettingsExpertModeChange);

    /* Chooser-pane connections: */
    connect(chooser(), &UIChooser::sigSelectionChanged,
            this, &UIGlobalToolsManagerWidget::sltHandleChooserPaneSelectionChange);
    connect(chooser(), &UIChooser::sigCloudProfileStateChange,
            this, &UIGlobalToolsManagerWidget::sltHandleCloudProfileStateChange);

    /* Tools-menu connections: */
    connect(toolMenu(), &UITools::sigSelectionChanged,
            this, &UIGlobalToolsManagerWidget::sltHandleToolsMenuIndexChange);

    /* Tools-pane connections: */
    connect(this, &UIGlobalToolsManagerWidget::sigToolMenuUpdate,
            this, &UIGlobalToolsManagerWidget::sltHandleGlobalToolMenuUpdate);
    connect(machineToolManager(), &UIMachineToolsManagerWidget::sigToolMenuUpdate,
            this, &UIGlobalToolsManagerWidget::sltHandleMachineToolMenuUpdate);
    connect(toolPaneMachine(), &UIToolPaneMachine::sigSwitchToActivityOverviewPane,
            this, &UIGlobalToolsManagerWidget::sltSwitchToActivitiesTool);
}

void UIGlobalToolsManagerWidget::loadSettings()
{
    /* Acquire & select tools currently chosen in the menu: */
    const UIToolType enmTypeGlobal = toolMenu()->toolsType(UIToolClass_Global);
    const UIToolType enmTypeMachine = toolMenu()->toolsType(UIToolClass_Machine);
    sltHandleToolsMenuIndexChange(enmTypeGlobal);
    sltHandleToolsMenuIndexChange(enmTypeMachine);

    /* Update tools restrictions: */
    emit sigToolMenuUpdate();
}

void UIGlobalToolsManagerWidget::cleanupConnections()
{
    /* Global COM event handlers: */
    disconnect(gEDataManager, &UIExtraDataManager::sigSettingsExpertModeChange,
               this, &UIGlobalToolsManagerWidget::sltHandleSettingsExpertModeChange);

    /* Chooser-pane connections: */
    disconnect(chooser(), &UIChooser::sigSelectionChanged,
               this, &UIGlobalToolsManagerWidget::sltHandleChooserPaneSelectionChange);
    disconnect(chooser(), &UIChooser::sigCloudProfileStateChange,
               this, &UIGlobalToolsManagerWidget::sltHandleCloudProfileStateChange);

    /* Tools-menu connections: */
    disconnect(toolMenu(), &UITools::sigSelectionChanged,
               this, &UIGlobalToolsManagerWidget::sltHandleToolsMenuIndexChange);

    /* Tools-pane connections: */
    disconnect(this, &UIGlobalToolsManagerWidget::sigToolMenuUpdate,
               this, &UIGlobalToolsManagerWidget::sltHandleGlobalToolMenuUpdate);
    disconnect(machineToolManager(), &UIMachineToolsManagerWidget::sigToolMenuUpdate,
               this, &UIGlobalToolsManagerWidget::sltHandleMachineToolMenuUpdate);
    disconnect(toolPaneMachine(), &UIToolPaneMachine::sigSwitchToActivityOverviewPane,
               this, &UIGlobalToolsManagerWidget::sltSwitchToActivitiesTool);
}

UITools *UIGlobalToolsManagerWidget::toolMenu() const
{
    return m_pMenu;
}

UIChooser *UIGlobalToolsManagerWidget::chooser() const
{
    return machineToolManager()->chooser();
}

UIToolPaneMachine *UIGlobalToolsManagerWidget::toolPaneMachine() const
{
    return machineToolManager()->toolPane();
}
