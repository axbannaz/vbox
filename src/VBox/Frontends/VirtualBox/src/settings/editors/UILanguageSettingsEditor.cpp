/* $Id$ */
/** @file
 * VBox Qt GUI - UILanguageSettingsEditor class implementation.
 */

/*
 * Copyright (C) 2006-2025 Oracle and/or its affiliates.
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
#include <QDir>
#include <QHeaderView>
#include <QPainter>
#include <QRegularExpression>
#include <QTranslator>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "QITreeWidget.h"
#include "UILanguageSettingsEditor.h"
#include "UITranslator.h"

/* Other VBox includes: */
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/path.h>


/** QITreeWidgetItem subclass representing language tree-widget item. */
class UILanguageItem : public QITreeWidgetItem
{
    Q_OBJECT;

public:

    /** Casts QTreeWidgetItem* to UILanguageItem* if possible. */
    static UILanguageItem *toItem(QTreeWidgetItem *pItem);
    /** Casts const QTreeWidgetItem* to const UILanguageItem* if possible. */
    static const UILanguageItem *toItem(const QTreeWidgetItem *pItem);

    /** Constructs language tree-widget item passing @a pParent to the base-class.
      * @param  translator  Brings the translator this item is related to.
      * @param  strId       Brings the language ID this item is related to.
      * @param  fBuiltIn    Brings whether the language this item related to is built in. */
    UILanguageItem(QITreeWidget *pParent, const QTranslator &translator,
                   const QString &strId, bool fBuiltIn);
    /** Constructs language tree-widget item passing @a pParent to the base-class.
      * @param  strId       Brings the language ID this item is related to.
      * @note   This is a constructor for an invalid language ID (i.e. when a
      *         language file is missing or corrupt). */
    UILanguageItem(QITreeWidget *pParent, const QString &strId);
    /** Constructs language tree-widget item passing @a pParent to the base-class.
      * @note   This is a constructor for a default language ID
      *         (column 1 will be set to QString()). */
    UILanguageItem(QITreeWidget *pParent);

    /** Returns the ID. */
    QString id() const { return m_strId; }
    /** Returns the language name. */
    QString languageName() const { return m_strLanguageName; }
    /** Returns the translator name. */
    QString translatorName() const { return m_strTranslatorName; }
    /** Returns whether this item is for built in language. */
    bool isBuiltIn() const { return m_fBuiltIn; }

    /** Returns whether this item is less than @a another one. */
    bool operator<(const QTreeWidgetItem &another) const RT_OVERRIDE RT_FINAL;

private:

    /** Performs translation using passed @a translator for a
      * passed @a pContext, @a pSourceText and @a pComment. */
    static QString tratra(const QTranslator &translator, const char *pContext,
                          const char *pSourceText, const char *pComment);

    /** Holds the ID. */
    QString     m_strId;
    /** Holds the language name. */
    QString     m_strLanguageName;
    /** Holds the translator name. */
    QString     m_strTranslatorName;
    /** Holds whether this item is for built in language. */
    const bool  m_fBuiltIn;
};


/*********************************************************************************************************************************
*   Class UILanguageItem implementation.                                                                                         *
*********************************************************************************************************************************/

/* static */
UILanguageItem *UILanguageItem::toItem(QTreeWidgetItem *pItem)
{
    /* Make sure alive QITreeWidgetItem passed: */
    if (!pItem || pItem->type() != ItemType)
        return 0;

    /* Acquire casted QITreeWidgetItem: */
    QITreeWidgetItem *pIntermediateItem = static_cast<QITreeWidgetItem*>(pItem);
    if (!pIntermediateItem)
        return 0;

    /* Return proper UILanguageItem: */
    return qobject_cast<UILanguageItem*>(pIntermediateItem);
}

/* static */
const UILanguageItem *UILanguageItem::toItem(const QTreeWidgetItem *pItem)
{
    /* Make sure alive QITreeWidgetItem passed: */
    if (!pItem || pItem->type() != ItemType)
        return 0;

    /* Acquire casted QITreeWidgetItem: */
    const QITreeWidgetItem *pIntermediateItem = static_cast<const QITreeWidgetItem*>(pItem);
    if (!pIntermediateItem)
        return 0;

    /* Return proper UILanguageItem: */
    return qobject_cast<const UILanguageItem*>(pIntermediateItem);
}

UILanguageItem::UILanguageItem(QITreeWidget *pParent, const QTranslator &translator,
                               const QString &strId, bool fBuiltIn)
    : QITreeWidgetItem(pParent)
    , m_fBuiltIn(fBuiltIn)
{
    /* Sanity check: */
    Assert(!strId.isEmpty());

    /* Note: context/source/comment arguments below must match strings used in UITranslator::languageName() and friends
     *       (the latter are the source of information for the lupdate tool that generates translation files). */

    const QString strNativeLanguage = tratra(translator, "@@@", "English", "Native language name");
    const QString strNativeCountry = tratra(translator, "@@@", "--", "Native language country name "
                                                                     "(empty if this language is for all countries)");

    const QString strEnglishLanguage = tratra(translator, "@@@", "English", "Language name, in English");
    const QString strEnglishCountry = tratra(translator, "@@@", "--", "Language country name, in English "
                                                                      "(empty if native country name is empty)");

    const QString strTranslatorName = tratra(translator, "@@@", "Oracle Corporation", "Comma-separated list of translators");

    /* Fetch information: */
    QString strItemName = strNativeLanguage;
    QString strLanguageName = strEnglishLanguage;
    if (!m_fBuiltIn)
    {
        if (strNativeCountry != "--")
            strItemName += " (" + strNativeCountry + ")";

        if (strEnglishCountry != "--")
            strLanguageName += " (" + strEnglishCountry + ")";

        if (strItemName != strLanguageName)
            strLanguageName = strItemName + " / " + strLanguageName;
    }
    else
    {
        strItemName += tr(" (built-in)", "Language");
        strLanguageName += tr(" (built-in)", "Language");
    }

    /* Init fields: */
    setText(0, strItemName);
    m_strId = strId;
    m_strLanguageName = strLanguageName;
    m_strTranslatorName = strTranslatorName;

    /* Current language appears in bold: */
    if (id() == UITranslator::languageId())
    {
        QFont fnt = font(0);
        fnt.setBold(true);
        setFont(0, fnt);
    }
}

UILanguageItem::UILanguageItem(QITreeWidget *pParent, const QString &strId)
    : QITreeWidgetItem(pParent)
    , m_fBuiltIn(false)
{
    /* Sanity check: */
    Assert(!strId.isEmpty());

    /* Init fields: */
    setText(0, QString("<%1>").arg(strId));
    m_strId = strId;
    m_strLanguageName = tr("<unavailable>", "Language");
    m_strTranslatorName = tr("<unknown>", "Author(s)");

    /* Invalid language appears in italic: */
    QFont fnt = font(0);
    fnt.setItalic(true);
    setFont(0, fnt);
}

UILanguageItem::UILanguageItem(QITreeWidget *pParent)
    : QITreeWidgetItem(pParent)
    , m_fBuiltIn(false)
{
    /* Init fields: */
    setText(0, tr("Default", "Language"));

    /* Default language item appears in italic: */
    QFont fnt = font(0);
    fnt.setItalic(true);
    setFont(0, fnt);
}

bool UILanguageItem::operator<(const QTreeWidgetItem &another) const
{
    /* Cast another item to language one: */
    const QTreeWidgetItem *pAnotherItem = &another;
    AssertPtrReturn(pAnotherItem, false);
    const UILanguageItem *pAnotherLanguageItem = toItem(pAnotherItem);
    AssertPtrReturn(pAnotherLanguageItem, false);

    /* Compare id and built-in flag: */
    if (id().isNull())
        return true;
    if (pAnotherLanguageItem->id().isNull())
        return false;
    if (isBuiltIn())
        return true;
    if (pAnotherLanguageItem->isBuiltIn())
        return false;

    /* Call to base-class: */
    return QITreeWidgetItem::operator<(another);
}

/* static */
QString UILanguageItem::tratra(const QTranslator &translator, const char *pContext,
                               const char *pSourceText, const char *pComment)
{
    QString strMsg = translator.translate(pContext, pSourceText, pComment);
    /* Return the source text if no translation is found: */
    if (strMsg.isEmpty())
        strMsg = QString(pSourceText);
    return strMsg;
}


/*********************************************************************************************************************************
*   Class UILanguageSettingsEditor implementation.                                                                               *
*********************************************************************************************************************************/

UILanguageSettingsEditor::UILanguageSettingsEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_fPolished(false)
    , m_pTreeWidget(0)
    , m_pLabelInfo(0)
{
    prepare();
}

void UILanguageSettingsEditor::setValue(const QString &strValue)
{
    /* Update cached value and
     * tree-widget if value has changed: */
    if (m_strValue != strValue)
    {
        m_strValue = strValue;
        if (m_pTreeWidget)
            reloadLanguageTree();
    }
}

QString UILanguageSettingsEditor::value() const
{
    UILanguageItem *pCurrentItem = m_pTreeWidget ? UILanguageItem::toItem(m_pTreeWidget->currentItem()) : 0;
    return pCurrentItem ? pCurrentItem->id() : m_strValue;
}

void UILanguageSettingsEditor::sltRetranslateUI()
{
    /* Translate tree-widget: */
    if (m_pTreeWidget)
    {
        m_pTreeWidget->setWhatsThis(tr("Available user interface languages. The effective language is written "
                                       "in bold. Select Default to reset to the system default language."));

        /* Update tree-widget contents finally: */
        reloadLanguageTree();
    }
}

void UILanguageSettingsEditor::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    UIEditor::showEvent(pEvent);

    /* Polish if necessary: */
    if (!m_fPolished)
    {
        polishEvent(pEvent);
        m_fPolished = true;
    }
}

void UILanguageSettingsEditor::polishEvent(QShowEvent * /* pEvent */)
{
    /* Remember current info-label width: */
    if (m_pLabelInfo)
        m_pLabelInfo->setMinimumTextWidth(m_pLabelInfo->width());
}

void UILanguageSettingsEditor::sltHandleItemPainting(QTreeWidgetItem *pItem, QPainter *pPainter)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pItem);
    AssertPtrReturnVoid(pPainter);
    AssertPtrReturnVoid(m_pTreeWidget);

    /* Cast item to language one: */
    UILanguageItem *pLanguageItem = UILanguageItem::toItem(pItem);
    AssertPtrReturnVoid(pLanguageItem);

    /* For built in language item: */
    if (pLanguageItem->isBuiltIn())
    {
        /* We are drawing a separator line in the tree: */
        const QRect rect = m_pTreeWidget->visualItemRect(pLanguageItem);
        pPainter->setPen(m_pTreeWidget->palette().color(QPalette::Window));
        pPainter->drawLine(rect.x(), rect.y() + rect.height() - 1,
                           rect.x() + rect.width(), rect.y() + rect.height() - 1);
    }
}

void UILanguageSettingsEditor::sltHandleCurrentItemChange(QTreeWidgetItem *pItem)
{
    /* Sanity check: */
    AssertPtrReturnVoid(m_pLabelInfo);

    /* Make sure item chosen: */
    if (!pItem)
        return;

    /* Cast item to language one: */
    UILanguageItem *pCurrentItem = UILanguageItem::toItem(pItem);
    AssertPtrReturnVoid(pCurrentItem);

    /* Disable labels for the Default language item: */
    const bool fEnabled = !pCurrentItem->id().isNull();
    m_pLabelInfo->setEnabled(fEnabled);
    m_pLabelInfo->setText(QString("<table>"
                                  "<tr><td>%1&nbsp;</td><td>%2</td></tr>"
                                  "<tr><td>%3&nbsp;</td><td>%4</td></tr>"
                                  "</table>")
                                  .arg(tr("Language:"))
                                  .arg(pCurrentItem->languageName())
                                  .arg(tr("Author(s):"))
                                  .arg(pCurrentItem->translatorName()));
}

void UILanguageSettingsEditor::prepare()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayoutMain = new QVBoxLayout(this);
    if (pLayoutMain)
    {
        pLayoutMain->setContentsMargins(0, 0, 0, 0);

        /* Prepare tree-widget: */
        m_pTreeWidget = new QITreeWidget(this);
        if (m_pTreeWidget)
        {
            m_pTreeWidget->header()->hide();
            m_pTreeWidget->setColumnCount(4);
            m_pTreeWidget->hideColumn(1);
            m_pTreeWidget->hideColumn(2);
            m_pTreeWidget->hideColumn(3);
            m_pTreeWidget->setRootIsDecorated(false);

            pLayoutMain->addWidget(m_pTreeWidget);
        }

        /* Prepare info label: */
        m_pLabelInfo = new QIRichTextLabel(this);
        if (m_pLabelInfo)
        {
            m_pLabelInfo->setWordWrapMode(QTextOption::WordWrap);
            m_pLabelInfo->setMinimumHeight(QFontMetrics(m_pLabelInfo->font(), m_pLabelInfo).height() * 5);

            pLayoutMain->addWidget(m_pLabelInfo);
        }
    }

    /* Prepare connections: */
    if (m_pTreeWidget)
    {
        connect(m_pTreeWidget, &QITreeWidget::painted, this, &UILanguageSettingsEditor::sltHandleItemPainting);
        connect(m_pTreeWidget, &QITreeWidget::currentItemChanged, this, &UILanguageSettingsEditor::sltHandleCurrentItemChange);
    }

    /* Apply language settings: */
    sltRetranslateUI();
}

void UILanguageSettingsEditor::reloadLanguageTree()
{
    /* Sanity check: */
    AssertPtrReturnVoid(m_pTreeWidget);

    /* Clear languages tree: */
    m_pTreeWidget->clear();

    /* Load languages tree: */
    char szNlsPath[RTPATH_MAX];
    const int rc = RTPathAppPrivateNoArch(szNlsPath, sizeof(szNlsPath));
    AssertRC(rc);
    const QString strNlsPath = QString(szNlsPath) + UITranslator::vboxLanguageSubDirectory();
    const QDir nlsDir(strNlsPath);
    const QStringList files = nlsDir.entryList(QStringList(QString("%1*%2").arg(UITranslator::vboxLanguageFileBase(),
                                                                                UITranslator::vboxLanguageFileExtension())),
                                               QDir::Files);

    QTranslator translator;
    /* Add the default language: */
    new UILanguageItem(m_pTreeWidget);
    /* Add the built-in language: */
    new UILanguageItem(m_pTreeWidget, translator, UITranslator::vboxBuiltInLanguageName(), true /* built-in */);
    /* Add all remaining languages: */
    foreach (const QString &strFileName, files)
    {
        /* Skip unmatched languages: */
        const QRegularExpression re(UITranslator::vboxLanguageFileBase() + UITranslator::vboxLanguageIdRegExp());
        const QRegularExpressionMatch mt = re.match(strFileName);
        if (!mt.hasMatch())
            continue;

        /* Skip any English version, cause this is extra handled: */
        QString strLanguage = mt.captured(2);
        if (strLanguage.toLower() == "en")
            continue;

        /* Make sure language loadable: */
        bool fLoadOk = translator.load(strFileName, strNlsPath);
        if (!fLoadOk)
            continue;

        /* Create corresponding language item: */
        new UILanguageItem(m_pTreeWidget, translator, mt.captured(1), false /* built-in */);
    }

    /* Search for a language to select: */
    UILanguageItem *pItem = 0;
    for (int i = 0; i < m_pTreeWidget->childCount(); ++i)
    {
        UILanguageItem *pLanguageItem = UILanguageItem::toItem(m_pTreeWidget->childItem(i));
        AssertPtr(pLanguageItem);
        if (pLanguageItem && pLanguageItem->id() == m_strValue)
            pItem = pLanguageItem;
    }

    /* Add an pItem for an invalid language to represent it in the list: */
    if (!pItem)
    {
        pItem = new UILanguageItem(m_pTreeWidget, m_strValue);
        m_pTreeWidget->resizeColumnToContents(0);
    }
    AssertPtrReturnVoid(pItem);

    /* Sort the tree and make current item visible: */
    m_pTreeWidget->sortItems(0, Qt::AscendingOrder);

    /* Select the language item and make it visible: */
    m_pTreeWidget->setCurrentItem(pItem);
    m_pTreeWidget->scrollToItem(pItem);
}


#include "UILanguageSettingsEditor.moc"
