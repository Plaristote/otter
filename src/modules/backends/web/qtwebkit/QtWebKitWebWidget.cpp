/**************************************************************************
* Otter Browser: Web browser controlled by the user, not vice-versa.
* Copyright (C) 2013 - 2014 Michal Dutkiewicz aka Emdek <michal@emdek.pl>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
**************************************************************************/

#include "QtWebKitWebWidget.h"
#include "QtWebKitWebPage.h"
#include "../../../windows/web/ImagePropertiesDialog.h"
#include "../../../../core/ActionsManager.h"
#include "../../../../core/HistoryManager.h"
#include "../../../../core/NetworkAccessManager.h"
#include "../../../../core/SearchesManager.h"
#include "../../../../core/SessionsManager.h"
#include "../../../../core/SettingsManager.h"
#include "../../../../core/TransfersManager.h"
#include "../../../../core/Utils.h"
#include "../../../../ui/ContentsWidget.h"
#include "../../../../ui/SearchPropertiesDialog.h"

#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QMouseEvent>
#include <QtGui/QMovie>
#include <QtNetwork/QAbstractNetworkCache>
#include <QtWebKit/QWebHistory>
#include <QtWebKit/QWebElement>
#include <QtWebKitWidgets/QWebFrame>
#include <QtWidgets/QApplication>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QMenu>
#include <QtWidgets/QToolTip>
#include <QtWidgets/QVBoxLayout>

namespace Otter
{

QtWebKitWebWidget::QtWebKitWebWidget(bool privateWindow, ContentsWidget *parent, QtWebKitWebPage *page) : WebWidget(parent),
	m_parent(parent),
	m_webView(new QWebView(this)),
	m_inspector(NULL),
	m_networkAccessManager(NULL),
	m_splitter(new QSplitter(Qt::Vertical, this)),
	m_searchEngine(SettingsManager::getValue(QLatin1String("Browser/DefaultSearchEngine")).toString()),
	m_historyEntry(-1),
	m_isLinkHovered(false),
	m_isLoading(false),
	m_isTyped(false)
{
	m_splitter->addWidget(m_webView);
	m_splitter->setChildrenCollapsible(false);
	m_splitter->setContentsMargins(0, 0, 0, 0);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(m_splitter);
	layout->setContentsMargins(0, 0, 0, 0);

	setLayout(layout);

	if (page)
	{
		page->setParent(this);
	}
	else
	{
		page = new QtWebKitWebPage(this);
	}

	m_networkAccessManager = new NetworkAccessManager(privateWindow, false, parent);
	m_networkAccessManager->setParent(page);

	page->setNetworkAccessManager(m_networkAccessManager);
	page->setForwardUnsupportedContent(true);

	m_webView->setPage(page);
	m_webView->setContextMenuPolicy(Qt::CustomContextMenu);
	m_webView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_webView->settings()->setAttribute(QWebSettings::PrivateBrowsingEnabled, privateWindow);
	m_webView->installEventFilter(this);

	ActionsManager::setupLocalAction(getAction(CutAction), QLatin1String("Cut"));
	ActionsManager::setupLocalAction(getAction(CopyAction), QLatin1String("Copy"));
	ActionsManager::setupLocalAction(getAction(PasteAction), QLatin1String("Paste"));
	ActionsManager::setupLocalAction(getAction(DeleteAction), QLatin1String("Delete"));
	ActionsManager::setupLocalAction(getAction(SelectAllAction), QLatin1String("SelectAll"));
	ActionsManager::setupLocalAction(getAction(UndoAction), QLatin1String("Undo"));
	ActionsManager::setupLocalAction(getAction(RedoAction), QLatin1String("Redo"));
	ActionsManager::setupLocalAction(getAction(GoBackAction), QLatin1String("GoBack"));
	ActionsManager::setupLocalAction(getAction(GoForwardAction), QLatin1String("GoForward"));
	ActionsManager::setupLocalAction(getAction(ReloadAction), QLatin1String("Reload"));
	ActionsManager::setupLocalAction(getAction(StopAction), QLatin1String("Stop"));
	ActionsManager::setupLocalAction(getAction(OpenLinkInThisTabAction), QLatin1String("OpenLinkInThisTab"));
	ActionsManager::setupLocalAction(getAction(OpenLinkInNewWindowAction), QLatin1String("OpenLinkInNewWindow"));
	ActionsManager::setupLocalAction(getAction(OpenFrameInNewTabAction), QLatin1String("OpenFrameInNewTab"));
	ActionsManager::setupLocalAction(getAction(SaveLinkToDiskAction), QLatin1String("SaveLinkToDisk"));
	ActionsManager::setupLocalAction(getAction(CopyLinkToClipboardAction), QLatin1String("CopyLinkToClipboard"));
	ActionsManager::setupLocalAction(getAction(OpenImageInNewTabAction), QLatin1String("OpenImageInNewTab"));
	ActionsManager::setupLocalAction(getAction(SaveImageToDiskAction), QLatin1String("SaveImageToDisk"));
	ActionsManager::setupLocalAction(getAction(CopyImageToClipboardAction), QLatin1String("CopyImageToClipboard"));
	ActionsManager::setupLocalAction(getAction(CopyImageUrlToClipboardAction), QLatin1String("CopyImageUrlToClipboard"));
	ActionsManager::setupLocalAction(getAction(SaveMediaToDiskAction), QLatin1String("SaveMediaToDisk"));
	ActionsManager::setupLocalAction(getAction(CopyMediaUrlToClipboardAction), QLatin1String("CopyMediaUrlToClipboard"));
	ActionsManager::setupLocalAction(getAction(ToggleMediaControlsAction), QLatin1String("ToggleMediaControls"));
	ActionsManager::setupLocalAction(getAction(ToggleMediaLoopAction), QLatin1String("ToggleMediaLoop"));
	ActionsManager::setupLocalAction(getAction(ToggleMediaPlayPauseAction), QLatin1String("ToggleMediaPlayPause"));
	ActionsManager::setupLocalAction(getAction(ToggleMediaMuteAction), QLatin1String("ToggleMediaMute"));

	getAction(ReloadAction)->setEnabled(true);
	getAction(OpenLinkInThisTabAction)->setIcon(Utils::getIcon(QLatin1String("document-open")));
	optionChanged(QLatin1String("History/BrowsingLimitAmountWindow"), SettingsManager::getValue(QLatin1String("History/BrowsingLimitAmountWindow")));
	setZoom(SettingsManager::getValue(QLatin1String("Content/DefaultZoom")).toInt());

	connect(SearchesManager::getInstance(), SIGNAL(searchEnginesModified()), this, SLOT(updateSearchActions()));
	connect(SettingsManager::getInstance(), SIGNAL(valueChanged(QString,QVariant)), this, SLOT(optionChanged(QString,QVariant)));
	connect(page, SIGNAL(requestedNewWindow(WebWidget*)), this, SIGNAL(requestedNewWindow(WebWidget*)));
	connect(page, SIGNAL(microFocusChanged()), this, SIGNAL(actionsChanged()));
	connect(page, SIGNAL(selectionChanged()), this, SIGNAL(actionsChanged()));
	connect(page, SIGNAL(loadStarted()), this, SLOT(pageLoadStarted()));
	connect(page, SIGNAL(loadFinished(bool)), this, SLOT(pageLoadFinished(bool)));
	connect(page, SIGNAL(statusBarMessage(QString)), this, SIGNAL(statusMessageChanged(QString)));
	connect(page, SIGNAL(linkHovered(QString,QString,QString)), this, SLOT(linkHovered(QString,QString)));
	connect(page, SIGNAL(saveFrameStateRequested(QWebFrame*,QWebHistoryItem*)), this, SLOT(saveState(QWebFrame*,QWebHistoryItem*)));
	connect(page, SIGNAL(restoreFrameStateRequested(QWebFrame*)), this, SLOT(restoreState(QWebFrame*)));
	connect(page, SIGNAL(downloadRequested(QNetworkRequest)), this, SLOT(downloadFile(QNetworkRequest)));
	connect(page, SIGNAL(unsupportedContent(QNetworkReply*)), this, SLOT(downloadFile(QNetworkReply*)));
	connect(page->mainFrame(), SIGNAL(contentsSizeChanged(QSize)), this, SIGNAL(progressBarGeometryChanged()));
	connect(page->mainFrame(), SIGNAL(initialLayoutCompleted()), this, SIGNAL(progressBarGeometryChanged()));
	connect(page->mainFrame(), SIGNAL(loadStarted()), this, SIGNAL(progressBarGeometryChanged()));
	connect(m_webView, SIGNAL(titleChanged(const QString)), this, SLOT(notifyTitleChanged()));
	connect(m_webView, SIGNAL(urlChanged(const QUrl)), this, SLOT(notifyUrlChanged(const QUrl)));
	connect(m_webView, SIGNAL(iconChanged()), this, SLOT(notifyIconChanged()));
	connect(m_webView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
	connect(m_networkAccessManager, SIGNAL(statusChanged(int,int,qint64,qint64,qint64)), this, SIGNAL(loadStatusChanged(int,int,qint64,qint64,qint64)));
	connect(m_networkAccessManager, SIGNAL(documentLoadProgressChanged(int)), this, SIGNAL(loadProgress(int)));
	connect(m_splitter, SIGNAL(splitterMoved(int,int)), this, SIGNAL(progressBarGeometryChanged()));
}

void QtWebKitWebWidget::showEvent(QShowEvent*)
{
  // Reset the thumbnail whenever the tab is focused
  if (!(m_thumbnail.isNull()))
    m_thumbnail = QPixmap();
}

void QtWebKitWebWidget::search(QAction *action)
{
	const QString engine = ((!action || action->data().type() != QVariant::String) ? m_searchEngine : action->data().toString());

	if (SearchesManager::getSearchEngines().contains(engine))
	{
		updateSearchActions(engine);

		emit requestedSearch(m_webView->selectedText(), m_searchEngine);
	}
}

void QtWebKitWebWidget::search(const QString &query, const QString &engine)
{
	QNetworkRequest request;
	QNetworkAccessManager::Operation method;
	QByteArray body;

	if (SearchesManager::setupSearchQuery(query, engine, &request, &method, &body))
	{
		m_webView->page()->mainFrame()->load(request, method, body);
	}
}

void QtWebKitWebWidget::print(QPrinter *printer)
{
	m_webView->print(printer);
}

void QtWebKitWebWidget::setQuickSearchEngine(const QString &searchEngine)
{
	if (searchEngine != m_searchEngine)
	{
		m_searchEngine = searchEngine;

		updateSearchActions();
	}
}

void QtWebKitWebWidget::optionChanged(const QString &option, const QVariant &value)
{
	if (option == QLatin1String("History/BrowsingLimitAmountWindow"))
	{
		m_webView->page()->history()->setMaximumItemCount(value.toInt());
	}
}

void QtWebKitWebWidget::pageLoadStarted()
{
	m_isLoading = true;

	m_thumbnail = QPixmap();

	if (m_actions.contains(RewindBackAction))
	{
		getAction(RewindBackAction)->setEnabled(getAction(GoBackAction)->isEnabled());
	}

	if (m_actions.contains(RewindForwardAction))
	{
		getAction(RewindForwardAction)->setEnabled(getAction(GoForwardAction)->isEnabled());
	}

	if (m_actions.contains(ReloadOrStopAction))
	{
		QAction *action = getAction(ReloadOrStopAction);

		ActionsManager::setupLocalAction(action, QLatin1String("Stop"));

		action->setShortcut(QKeySequence());
		action->setEnabled(true);
	}

	if (!isPrivate())
	{
		SessionsManager::markSessionModified();

		m_historyEntry = HistoryManager::addEntry(getUrl(), m_webView->title(), m_webView->icon(), m_isTyped);

		m_isTyped = false;
	}

	emit loadingChanged(true);
	emit statusMessageChanged(QString());
}

void QtWebKitWebWidget::pageLoadFinished(bool ok)
{
	if (!m_isLoading)
	{
		return;
	}

	m_isLoading = false;

	m_thumbnail = QPixmap();

	m_networkAccessManager->resetStatistics();

	if (m_actions.contains(ReloadOrStopAction))
	{
		QAction *action = getAction(ReloadOrStopAction);

		ActionsManager::setupLocalAction(action, QLatin1String("Reload"));

		action->setShortcut(QKeySequence());
	}

	if (!isPrivate())
	{
		if (ok)
		{
			SessionsManager::markSessionModified();

			if (m_historyEntry >= 0)
			{
				HistoryManager::updateEntry(m_historyEntry, getUrl(), m_webView->title(), m_webView->icon());
			}
		}
		else if (m_historyEntry >= 0)
		{
			HistoryManager::removeEntry(m_historyEntry);
		}
	}


	emit loadingChanged(false);
}

void QtWebKitWebWidget::downloadFile(const QNetworkRequest &request)
{
	TransfersManager::startTransfer(request, QString(), isPrivate());
}

void QtWebKitWebWidget::downloadFile(QNetworkReply *reply)
{
	TransfersManager::startTransfer(reply, QString(), isPrivate());
}

void QtWebKitWebWidget::linkHovered(const QString &link, const QString &title)
{
	QString text;

	if (!link.isEmpty())
	{
		text = (title.isEmpty() ? tr("Address: %1").arg(link) : tr("Title: %1\nAddress: %2").arg(title).arg(link));
	}

	m_isLinkHovered = !text.isEmpty();

	QToolTip::showText(QCursor::pos(), text, m_webView);

	emit statusMessageChanged(link, 0);
}

void QtWebKitWebWidget::saveState(QWebFrame *frame, QWebHistoryItem *item)
{
	if (frame == m_webView->page()->mainFrame())
	{
		QVariantHash data;
		data[QLatin1String("position")] = m_webView->page()->mainFrame()->scrollPosition();
		data[QLatin1String("zoom")] = getZoom();

		item->setUserData(data);
	}
}

void QtWebKitWebWidget::restoreState(QWebFrame *frame)
{
	if (frame == m_webView->page()->mainFrame())
	{
		setZoom(m_webView->history()->currentItem().userData().toHash().value(QLatin1String("zoom"), getZoom()).toInt());

		if (m_webView->page()->mainFrame()->scrollPosition() == QPoint(0, 0))
		{
			m_webView->page()->mainFrame()->setScrollPosition(m_webView->history()->currentItem().userData().toHash().value(QLatin1String("position")).toPoint());
		}
	}
}

void QtWebKitWebWidget::searchMenuAboutToShow()
{
	QAction *searchMenuAction = getAction(SearchMenuAction);

	if (searchMenuAction->isEnabled() && searchMenuAction->menu()->actions().isEmpty())
	{
		const QStringList engines = SearchesManager::getSearchEngines();

		for (int i = 0; i < engines.count(); ++i)
		{
			SearchInformation *search = SearchesManager::getSearchEngine(engines.at(i));

			if (search)
			{
				QAction *searchAction = searchMenuAction->menu()->addAction(search->icon, search->title);
				searchAction->setData(search->identifier);
				searchAction->setToolTip(search->description);
			}
		}
	}
}

void QtWebKitWebWidget::notifyTitleChanged()
{
	emit titleChanged(getTitle());
}

void QtWebKitWebWidget::notifyUrlChanged(const QUrl &url)
{
	if (m_actions.contains(RewindBackAction))
	{
		getAction(RewindBackAction)->setEnabled(getAction(GoBackAction)->isEnabled());
	}

	if (m_actions.contains(RewindForwardAction))
	{
		getAction(RewindForwardAction)->setEnabled(getAction(GoForwardAction)->isEnabled());
	}

	emit urlChanged(url);
}

void QtWebKitWebWidget::notifyIconChanged()
{
	emit iconChanged(getIcon());
}

void QtWebKitWebWidget::updateSearchActions(const QString &engine)
{
	if (sender() == SearchesManager::getInstance())
	{
		getAction(SearchMenuAction)->menu()->clear();
	}

	QAction *defaultSearchAction = getAction(SearchAction);
	SearchInformation *search = SearchesManager::getSearchEngine(engine.isEmpty() ? m_searchEngine : engine);

	if (!search)
	{
		search = SearchesManager::getSearchEngine(SearchesManager::getSearchEngines().value(0, QString()));
	}

	if (search)
	{
		defaultSearchAction->setEnabled(true);
		defaultSearchAction->setIcon(search->icon.isNull() ? Utils::getIcon(QLatin1String("edit-find")) : search->icon);
		defaultSearchAction->setText(search->title);
		defaultSearchAction->setToolTip(search->description);

		m_searchEngine = search->identifier;
	}
	else
	{
		defaultSearchAction->setEnabled(false);
		defaultSearchAction->setIcon(QIcon());
		defaultSearchAction->setText(tr("Search"));
		defaultSearchAction->setToolTip(tr("No search engines defined"));
	}

	getAction(SearchMenuAction)->setEnabled(SearchesManager::getSearchEngines().count() > 1);
}

void QtWebKitWebWidget::showDialog(QWidget *dialog)
{
	m_parent->showDialog(dialog);
}

void QtWebKitWebWidget::hideDialog(QWidget *dialog)
{
	m_parent->hideDialog(dialog);
}

void QtWebKitWebWidget::goToHistoryIndex(int index)
{
	m_webView->history()->goToItem(m_webView->history()->itemAt(index));
}

void QtWebKitWebWidget::triggerAction()
{
	QAction *action = qobject_cast<QAction*>(sender());

	if (action)
	{
		triggerAction(static_cast<WindowAction>(action->data().toInt()));
	}
}

void QtWebKitWebWidget::triggerAction(WindowAction action, bool checked)
{
	const QWebPage::WebAction webAction = mapAction(action);

	if (webAction != QWebPage::NoWebAction)
	{
		m_webView->triggerPageAction(webAction, checked);

		return;
	}

	switch (action)
	{
		case RewindBackAction:
			m_webView->page()->history()->goToItem(m_webView->page()->history()->itemAt(0));

			break;
		case RewindForwardAction:
			m_webView->page()->history()->goToItem(m_webView->page()->history()->itemAt(m_webView->page()->history()->count() - 1));

			break;
		case CopyAddressAction:
			QApplication::clipboard()->setText(getUrl().toString());

			break;
		case ZoomInAction:
			setZoom(qMin((getZoom() + 10), 10000));

			break;
		case ZoomOutAction:
			setZoom(qMax((getZoom() - 10), 10));

			break;
		case ZoomOriginalAction:
			setZoom(100);

			break;
		case ReloadOrStopAction:
			if (isLoading())
			{
				triggerAction(StopAction);
			}
			else
			{
				triggerAction(ReloadAction);
			}

			break;
		case OpenLinkInNewTabAction:
			if (m_hitResult.linkUrl().isValid())
			{
				emit requestedOpenUrl(m_hitResult.linkUrl(), false, false);
			}

			break;
		case OpenLinkInNewTabBackgroundAction:
			if (m_hitResult.linkUrl().isValid())
			{
				emit requestedOpenUrl(m_hitResult.linkUrl(), true, false);
			}

			break;
		case OpenLinkInNewWindowAction:
			if (m_hitResult.linkUrl().isValid())
			{
				emit requestedOpenUrl(m_hitResult.linkUrl(), false, true);
			}

			break;
		case OpenLinkInNewWindowBackgroundAction:
			if (m_hitResult.linkUrl().isValid())
			{
				emit requestedOpenUrl(m_hitResult.linkUrl(), true, true);
			}

			break;
		case BookmarkLinkAction:
			if (m_hitResult.linkUrl().isValid())
			{
				emit requestedAddBookmark(m_hitResult.linkUrl(), m_hitResult.element().attribute(QLatin1String("title")));
			}

			break;
		case OpenSelectionAsLinkAction:
			emit requestedOpenUrl(m_webView->selectedText(), false, false);

			break;
		case ImagePropertiesAction:
			{
				ImagePropertiesDialog dialog(m_hitResult.imageUrl(), m_hitResult.element().attribute(QLatin1String("alt")), m_hitResult.element().attribute(QLatin1String("longdesc")), m_hitResult.pixmap(), (m_networkAccessManager->cache() ? m_networkAccessManager->cache()->data(m_hitResult.imageUrl()) : NULL), this);
				QEventLoop eventLoop;

				m_parent->showDialog(&dialog);

				connect(&dialog, SIGNAL(finished(int)), &eventLoop, SLOT(quit()));
				connect(this, SIGNAL(destroyed()), &eventLoop, SLOT(quit()));

				eventLoop.exec();

				m_parent->hideDialog(&dialog);
			}

			break;
		case InspectPageAction:
			if (!m_inspector)
			{
				m_inspector = new QWebInspector(this);
				m_inspector->setPage(m_webView->page());
				m_inspector->setContextMenuPolicy(Qt::NoContextMenu);
				m_inspector->setMinimumHeight(200);

				m_splitter->addWidget(m_inspector);
			}

			m_inspector->setVisible(checked);

			getAction(InspectPageAction)->setChecked(checked);

			emit actionsChanged();
			emit progressBarGeometryChanged();

			break;
		case SaveLinkToDownloadsAction:
			TransfersManager::startTransfer(m_hitResult.linkUrl().toString(), QString(), isPrivate(), true);

			break;
		case OpenFrameInThisTabAction:
			if (m_hitResult.frame())
			{
				setUrl(m_hitResult.frame()->url().isValid() ? m_hitResult.frame()->url() : m_hitResult.frame()->requestedUrl());
			}

			break;
		case OpenFrameInNewTabBackgroundAction:
			if (m_hitResult.frame())
			{
				emit requestedOpenUrl((m_hitResult.frame()->url().isValid() ? m_hitResult.frame()->url() : m_hitResult.frame()->requestedUrl()), true, false);
			}

			break;
		case CopyFrameLinkToClipboardAction:
			if (m_hitResult.frame())
			{
				QGuiApplication::clipboard()->setText((m_hitResult.frame()->url().isValid() ? m_hitResult.frame()->url() : m_hitResult.frame()->requestedUrl()).toString());
			}

			break;
		case ReloadFrameAction:
			if (m_hitResult.frame())
			{
				const QUrl url = (m_hitResult.frame()->url().isValid() ? m_hitResult.frame()->url() : m_hitResult.frame()->requestedUrl());

				m_hitResult.frame()->setUrl(QUrl());
				m_hitResult.frame()->setUrl(url);
			}

			break;
		case SearchAction:
			search(getAction(SearchAction));

			break;
		case CreateSearchAction:
			{
				QWebElement parentElement = m_hitResult.element().parent();

				while (!parentElement.isNull() && parentElement.tagName().toLower() != "form")
				{
					parentElement = parentElement.parent();
				}

				const QWebElementCollection inputs = parentElement.findAll(QLatin1String("input:not([disabled])[name], select:not([disabled])[name], textarea:not([disabled])[name]"));

				if (!parentElement.isNull() && parentElement.hasAttribute(QLatin1String("action")) && inputs.count() > 0)
				{
					QUrlQuery parameters;

					for (int i = 0; i < inputs.count(); ++i)
					{
						QString value;

						if (inputs.at(i).tagName().toLower() == QLatin1String("textarea"))
						{
							value = inputs.at(i).toPlainText();
						}
						else if (inputs.at(i).tagName().toLower() == QLatin1String("select"))
						{
							const QWebElementCollection options = inputs.at(i).findAll(QLatin1String("option"));

							for (int j = 0; j < options.count(); ++j)
							{
								if (options.at(j).hasAttribute(QLatin1String("selected")))
								{
									value = options.at(j).attribute(QLatin1String("value"), options.at(j).toPlainText());

									break;
								}
							}
						}
						else
						{
							if ((inputs.at(i).attribute(QLatin1String("type")) == QLatin1String("checkbox") || inputs.at(i).attribute(QLatin1String("type")) == QLatin1String("radio")) && !inputs.at(i).hasAttribute(QLatin1String("checked")))
							{
								continue;
							}

							value = inputs.at(i).attribute(QLatin1String("value"));
						}

						parameters.addQueryItem(inputs.at(i).attribute(QLatin1String("name")), ((inputs.at(i) == m_hitResult.element()) ? QLatin1String("{searchTerms}") : value));
					}

					const QStringList identifiers = SearchesManager::getSearchEngines();
					QStringList shortcuts;
					QList<SearchInformation*> engines;

					for (int i = 0; i < identifiers.count(); ++i)
					{
						SearchInformation *engine = SearchesManager::getSearchEngine(identifiers.at(i));

						if (!engine)
						{
							continue;
						}

						engines.append(engine);

						const QString shortcut = engine->shortcut;

						if (!shortcut.isEmpty())
						{
							shortcuts.append(shortcut);
						}
					}

					QString identifier = getUrl().host().toLower().remove(QRegularExpression(QLatin1String("[^a-z0-9]")));

					while (identifier.isEmpty() || identifiers.contains(identifier))
					{
						identifier = QInputDialog::getText(this, tr("Select Identifier"), tr("Input Unique Search Engine Identifier:"));

						if (identifier.isEmpty())
						{
							return;
						}
					}

					const QIcon icon = m_webView->icon();
					const QUrl url(parentElement.attribute(QLatin1String("action")));
					QVariantHash engineData;
					engineData[QLatin1String("identifier")] = identifier;
					engineData[QLatin1String("isDefault")] = false;
					engineData[QLatin1String("encoding")] = QLatin1String("UTF-8");
					engineData[QLatin1String("selfUrl")] = QString();
					engineData[QLatin1String("resultsUrl")] = (url.isEmpty() ? getUrl() : (url.isRelative() ? getUrl().resolved(url) : url)).toString();
					engineData[QLatin1String("resultsEnctype")] = parentElement.attribute(QLatin1String("enctype"));
					engineData[QLatin1String("resultsMethod")] = ((parentElement.attribute(QLatin1String("method"), QLatin1String("get")).toLower() == QLatin1String("post")) ? QLatin1String("post") : QLatin1String("get"));
					engineData[QLatin1String("resultsParameters")] = parameters.toString(QUrl::FullyDecoded);
					engineData[QLatin1String("suggestionsUrl")] = QString();
					engineData[QLatin1String("suggestionsEnctype")] = QString();
					engineData[QLatin1String("suggestionsMethod")] = QLatin1String("get");
					engineData[QLatin1String("suggestionsParameters")] = QString();
					engineData[QLatin1String("shortcut")] = QString();
					engineData[QLatin1String("title")] = getTitle();
					engineData[QLatin1String("description")] = QString();
					engineData[QLatin1String("icon")] = (icon.isNull() ? Utils::getIcon(QLatin1String("edit-find")) : icon);

					SearchPropertiesDialog dialog(engineData, shortcuts, this);

					if (dialog.exec() == QDialog::Rejected)
					{
						return;
					}

					engineData = dialog.getEngineData();

					if (shortcuts.contains(engineData[QLatin1String("shortcut")].toString()))
					{
						engineData[QLatin1String("shortcut")] = QString();
					}

					SearchInformation *engine = new SearchInformation();
					engine->identifier = engineData[QLatin1String("identifier")].toString();
					engine->title = engineData[QLatin1String("title")].toString();
					engine->description = engineData[QLatin1String("description")].toString();
					engine->shortcut = engineData[QLatin1String("shortcut")].toString();
					engine->encoding = engineData[QLatin1String("encoding")].toString();
					engine->selfUrl = engineData[QLatin1String("selfUrl")].toString();
					engine->resultsUrl.url = engineData[QLatin1String("resultsUrl")].toString();
					engine->resultsUrl.enctype = engineData[QLatin1String("resultsEnctype")].toString();
					engine->resultsUrl.method = engineData[QLatin1String("resultsMethod")].toString();
					engine->resultsUrl.parameters = QUrlQuery(engineData[QLatin1String("resultsParameters")].toString());
					engine->icon = engineData[QLatin1String("icon")].value<QIcon>();

					engines.append(engine);

					if (SearchesManager::setSearchEngines(engines) && engineData[QLatin1String("isDefault")].toBool())
					{
						SettingsManager::setValue(QLatin1String("Browser/DefaultSearchEngine"), engineData[QLatin1String("identifier")].toString());
					}
				}
			}

			break;
		case ClearAllAction:
			triggerAction(SelectAllAction);
			triggerAction(DeleteAction);

			break;
		default:
			break;
	}
}

void QtWebKitWebWidget::setDefaultTextEncoding(const QString &encoding)
{
	m_webView->settings()->setDefaultTextEncoding(encoding);
	m_webView->reload();
}

void QtWebKitWebWidget::setHistory(const WindowHistoryInformation &history)
{
	if (history.entries.count() == 0)
	{
		m_webView->page()->history()->clear();

		return;
	}

	qint64 documentSequence = 0;
	qint64 itemSequence = 0;
	QByteArray data;
	QDataStream stream(&data, QIODevice::ReadWrite);
	stream << int(2) << history.entries.count() << history.index;

	for (int i = 0; i < history.entries.count(); ++i)
	{
		stream << history.entries.at(i).url << history.entries.at(i).title << history.entries.at(i).url << quint32(2) << quint64(0) << ++documentSequence << quint64(0) << QString() << false << ++itemSequence << QString() << qint32(history.entries.at(i).position.x()) << qint32(history.entries.at(i).position.y()) << qreal((qreal) history.entries.at(i).zoom / 100) << false << QString() << false;
	}

	stream.device()->reset();
	stream >> *(m_webView->page()->history());

	for (int i = 0; i < history.entries.count(); ++i)
	{
		QVariantHash data;
		data[QLatin1String("position")] = history.entries.at(i).position;
		data[QLatin1String("zoom")] = history.entries.at(i).zoom;

		m_webView->page()->history()->itemAt(i).setUserData(data);
	}

	m_webView->page()->history()->goToItem(m_webView->page()->history()->itemAt(history.index));
}

void QtWebKitWebWidget::setZoom(int zoom)
{
	if (zoom != getZoom())
	{
		m_webView->setZoomFactor(qBound(0.1, ((qreal) zoom / 100), (qreal) 100));

		emit zoomChanged(zoom);
	}
}

void QtWebKitWebWidget::setUrl(const QUrl &url, bool typed)
{
	if (url.scheme() == QLatin1String("javascript"))
	{
		evaluateJavaScript(url.path());

		return;
	}

	if (!url.fragment().isEmpty() && url.matches(getUrl(), (QUrl::RemoveFragment | QUrl::StripTrailingSlash | QUrl::NormalizePathSegments)))
	{
		m_webView->page()->mainFrame()->scrollToAnchor(url.fragment());

		return;
	}

	m_isTyped = typed;

	if (url.isValid() && url.scheme().isEmpty() && !url.path().startsWith('/'))
	{
		QUrl httpUrl = url;
		httpUrl.setScheme(QLatin1String("http"));

		m_webView->setUrl(httpUrl);
	}
	else if (url.isValid() && (url.scheme().isEmpty() || url.scheme() == "file"))
	{
		QUrl localUrl = url;
		localUrl.setScheme(QLatin1String("file"));

		m_webView->setUrl(localUrl);
	}
	else
	{
		m_webView->setUrl(url);
	}

	notifyTitleChanged();
	notifyIconChanged();
}

void QtWebKitWebWidget::showContextMenu(const QPoint &position)
{
	if (position.isNull() && m_webView->selectedText().isEmpty())
	{
		return;
	}

	const QPoint hitPosition = (position.isNull() ? m_webView->mapFromGlobal(QCursor::pos()) : position);
	MenuFlags flags = NoMenu;

	m_hitResult = m_webView->page()->frameAt(hitPosition)->hitTestContent(hitPosition);

	if (m_hitResult.element().tagName().toLower() == QLatin1String("textarea") || m_hitResult.element().tagName().toLower() == QLatin1String("select") || (m_hitResult.element().tagName().toLower() == QLatin1String("input") && (m_hitResult.element().attribute(QLatin1String("type")).isEmpty() || m_hitResult.element().attribute(QLatin1String("type")).toLower() == QLatin1String("text"))))
	{
		QWebElement parentElement = m_hitResult.element().parent();

		while (!parentElement.isNull() && parentElement.tagName().toLower() != QLatin1String("form"))
		{
			parentElement = parentElement.parent();
		}

		if (!parentElement.isNull() && parentElement.hasAttribute(QLatin1String("action")) && !parentElement.findFirst(QLatin1String("input[name], select[name], textarea[name]")).isNull())
		{
			flags |= FormMenu;
		}
	}

	if (m_hitResult.pixmap().isNull() && m_hitResult.isContentSelected() && !m_webView->selectedText().isEmpty())
	{
		updateSearchActions(m_searchEngine);

		flags |= SelectionMenu;
	}

	if (m_hitResult.linkUrl().isValid())
	{
		flags |= LinkMenu;
	}

	if (!m_hitResult.pixmap().isNull())
	{
		flags |= ImageMenu;

		const bool isImageOpened = getUrl().matches(m_hitResult.imageUrl(), (QUrl::NormalizePathSegments | QUrl::RemoveFragment | QUrl::StripTrailingSlash));

		getAction(OpenImageInNewTabAction)->setEnabled(!isImageOpened);
		getAction(InspectElementAction)->setEnabled(!isImageOpened);
	}

	if (m_hitResult.mediaUrl().isValid())
	{
		flags |= MediaMenu;

		const bool isVideo = (m_hitResult.element().tagName().toLower() == QLatin1String("video"));
		const bool isPaused = m_hitResult.element().evaluateJavaScript(QLatin1String("this.paused")).toBool();
		const bool isMuted = m_hitResult.element().evaluateJavaScript(QLatin1String("this.muted")).toBool();

		getAction(SaveMediaToDiskAction)->setText(isVideo ? tr("Save Video...") : tr("Save Audio..."));
		getAction(CopyMediaUrlToClipboardAction)->setText(isVideo ? tr("Copy Video Link to Clipboard") : tr("Copy Audio Link to Clipboard"));
		getAction(ToggleMediaControlsAction)->setText(tr("Show Controls"));
		getAction(ToggleMediaLoopAction)->setText(tr("Looping"));
		getAction(ToggleMediaPlayPauseAction)->setIcon(Utils::getIcon(isPaused ? QLatin1String("media-playback-start") : QLatin1String("media-playback-pause")));
		getAction(ToggleMediaPlayPauseAction)->setText(isPaused ? tr("Play") : tr("Pause"));
		getAction(ToggleMediaMuteAction)->setIcon(Utils::getIcon(isMuted ? QLatin1String("audio-volume-medium") : QLatin1String("audio-volume-muted")));
		getAction(ToggleMediaMuteAction)->setText(isMuted ? tr("Unmute") : tr("Mute"));
	}

	if (m_hitResult.isContentEditable())
	{
		flags |= EditMenu;

		getAction(ClearAllAction)->setEnabled(getAction(SelectAllAction)->isEnabled());
	}

	if (flags == NoMenu || flags == FormMenu)
	{
		flags |= StandardMenu;

		if (m_hitResult.frame() != m_webView->page()->mainFrame())
		{
			flags |= FrameMenu;
		}
	}

	WebWidget::showContextMenu(hitPosition, flags);
}

WebWidget* QtWebKitWebWidget::clone(ContentsWidget *parent)
{
	QtWebKitWebWidget *widget = new QtWebKitWebWidget(isPrivate(), parent);
	widget->setDefaultTextEncoding(getDefaultTextEncoding());
	widget->setQuickSearchEngine(m_searchEngine);
	widget->setUrl(getUrl());
	widget->setHistory(getHistory());
	widget->setZoom(getZoom());

	return widget;
}

QAction* QtWebKitWebWidget::getAction(WindowAction action)
{
	const QWebPage::WebAction webAction = mapAction(action);

	if (webAction != QWebPage::NoWebAction)
	{
		return m_webView->page()->action(webAction);
	}

	if (action == NoAction)
	{
		return NULL;
	}

	if (m_actions.contains(action))
	{
		return m_actions[action];
	}

	QAction *actionObject = new QAction(this);
	actionObject->setData(action);

	connect(actionObject, SIGNAL(triggered()), this, SLOT(triggerAction()));

	switch (action)
	{
		case OpenLinkInNewTabAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("OpenLinkInNewTab"), true);

			break;
		case OpenLinkInNewTabBackgroundAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("OpenLinkInNewTabBackground"), true);

			break;
		case OpenLinkInNewWindowAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("OpenLinkInNewWindow"), true);

			break;
		case OpenLinkInNewWindowBackgroundAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("OpenLinkInNewWindowBackground"), true);

			break;
		case OpenFrameInThisTabAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("OpenFrameInThisTab"), true);

			break;
		case OpenFrameInNewTabBackgroundAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("OpenFrameInNewTabBackground"), true);

			break;
		case CopyFrameLinkToClipboardAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("CopyFrameLinkToClipboard"), true);

			break;
		case ViewSourceFrameAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("ViewSourceFrame"), true);

			actionObject->setEnabled(false);

			break;
		case ReloadFrameAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("ReloadFrame"), true);

			break;
		case SaveLinkToDownloadsAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("SaveLinkToDownloads"));

			break;
		case RewindBackAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("RewindBack"), true);

			actionObject->setEnabled(getAction(GoBackAction)->isEnabled());

			break;
		case RewindForwardAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("RewindForward"), true);

			actionObject->setEnabled(getAction(GoForwardAction)->isEnabled());

			break;
		case ReloadTimeAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("ReloadTime"), true);

			actionObject->setMenu(new QMenu(this));
			actionObject->setEnabled(false);

			break;
		case PrintAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("Print"), true);

			break;
		case BookmarkAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("AddBookmark"), true);

			break;
		case BookmarkLinkAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("BookmarkLink"), true);

			break;
		case CopyAddressAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("CopyAddress"), true);

			break;
		case ViewSourceAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("ViewSource"), true);

			actionObject->setEnabled(false);

			break;
		case ValidateAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("Validate"), true);

			actionObject->setMenu(new QMenu(this));
			actionObject->setEnabled(false);

			break;
		case ContentBlockingAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("ContentBlocking"), true);

			actionObject->setEnabled(false);

			break;
		case WebsitePreferencesAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("WebsitePreferences"), true);

			actionObject->setEnabled(false);

			break;
		case FullScreenAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("FullScreen"), true);

			actionObject->setEnabled(false);

			break;
		case ZoomInAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("ZoomIn"));

			break;
		case ZoomOutAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("ZoomOut"), true);

			break;
		case ZoomOriginalAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("ZoomOriginal"), true);

			break;
		case SearchAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("Search"), true);

			break;
		case SearchMenuAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("SearchMenu"), true);

			actionObject->setMenu(new QMenu(this));

			connect(actionObject->menu(), SIGNAL(aboutToShow()), this, SLOT(searchMenuAboutToShow()));
			connect(actionObject->menu(), SIGNAL(triggered(QAction*)), this, SLOT(search(QAction*)));

			break;
		case OpenSelectionAsLinkAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("OpenSelectionAsLink"), true);

			break;
		case ClearAllAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("ClearAll"), true);

			break;
		case SpellCheckAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("SpellCheck"), true);

			actionObject->setEnabled(false);

			break;
		case ImagePropertiesAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("ImageProperties"), true);

			break;
		case CreateSearchAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("CreateSearch"), true);

			break;
		case ReloadOrStopAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("Reload"));

			actionObject->setEnabled(true);
			actionObject->setShortcut(QKeySequence());

			break;
		case InspectPageAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("InspectPage"));

			actionObject->setEnabled(true);
			actionObject->setShortcut(QKeySequence());

			break;
		case FindAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("Find"), true);

			actionObject->setEnabled(true);

			break;
		case FindNextAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("FindNext"), true);

			actionObject->setEnabled(true);

			break;
		case FindPreviousAction:
			ActionsManager::setupLocalAction(actionObject, QLatin1String("FindPrevious"), true);

			actionObject->setEnabled(true);

			break;
		default:
			actionObject->deleteLater();
			actionObject = NULL;

			break;
	}

	if (actionObject)
	{
		m_actions[action] = actionObject;
	}

	return actionObject;
}

QUndoStack* QtWebKitWebWidget::getUndoStack()
{
	return m_webView->page()->undoStack();
}

QString QtWebKitWebWidget::getDefaultTextEncoding() const
{
	return m_webView->settings()->defaultTextEncoding();
}

QString QtWebKitWebWidget::getTitle() const
{
	const QString title = m_webView->title();

	if (title.isEmpty())
	{
		const QUrl url = getUrl();

		if (url.scheme() == QLatin1String("about") && (url.path().isEmpty() || url.path() == QLatin1String("blank")))
		{
			return tr("New Tab");
		}

		if (url.isLocalFile())
		{
			return QFileInfo(url.toLocalFile()).canonicalFilePath();
		}

		return tr("(Untitled)");
	}

	return title;
}

QString QtWebKitWebWidget::getSearchEngine() const
{
	return m_searchEngine;
}

QVariant QtWebKitWebWidget::evaluateJavaScript(const QString &script)
{
	return m_webView->page()->mainFrame()->evaluateJavaScript(script);
}

QUrl QtWebKitWebWidget::getUrl() const
{
	return m_webView->url();
}

QIcon QtWebKitWebWidget::getIcon() const
{
	if (isPrivate())
	{
		return Utils::getIcon(QLatin1String("tab-private"));
	}

	const QIcon icon = m_webView->icon();

	return (icon.isNull() ? Utils::getIcon(QLatin1String("tab")) : icon);
}

QPixmap QtWebKitWebWidget::getThumbnail()
{
	if (!m_thumbnail.isNull() && !isLoading())
	{
		return m_thumbnail;
	}

	const QSize thumbnailSize = QSize(260, 170);
	const QSize oldViewportSize = m_webView->page()->viewportSize();
	const qreal zoom = m_webView->page()->mainFrame()->zoomFactor();
	QSize contentsSize = m_webView->page()->mainFrame()->contentsSize();
	QWidget *newView = new QWidget();
	QWidget *oldView = m_webView->page()->view();

	m_webView->page()->setView(newView);
	m_webView->page()->setViewportSize(contentsSize);
	m_webView->page()->mainFrame()->setZoomFactor(1);

	if (contentsSize.width() > 2000)
	{
		contentsSize.setWidth(2000);
	}

	contentsSize.setHeight(thumbnailSize.height() * (qreal(contentsSize.width()) / thumbnailSize.width()));

	QPixmap pixmap(contentsSize);
	pixmap.fill(Qt::white);

	QPainter painter(&pixmap);

	m_webView->page()->mainFrame()->render(&painter, QWebFrame::ContentsLayer, QRegion(QRect(QPoint(0, 0), contentsSize)));
	m_webView->page()->mainFrame()->setZoomFactor(zoom);
	m_webView->page()->setView(oldView);
	m_webView->page()->setViewportSize(oldViewportSize);

	painter.end();

	pixmap = pixmap.scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

	newView->deleteLater();

	m_thumbnail = pixmap;

	return pixmap;
}

QRect QtWebKitWebWidget::getProgressBarGeometry() const
{
	QRect geometry(QPoint(0, (height() - ((m_inspector && m_inspector->isVisible()) ? m_inspector->height() : 0) - 30)), QSize(width(), 30));
	const QRect horizontalScrollBar = m_webView->page()->mainFrame()->scrollBarGeometry(Qt::Horizontal);
	const QRect verticalScrollBar = m_webView->page()->mainFrame()->scrollBarGeometry(Qt::Vertical);

	if (horizontalScrollBar.height() > 0 && geometry.intersects(horizontalScrollBar))
	{
		geometry.moveTop(m_webView->height() - 30 - horizontalScrollBar.height());
	}

	if (verticalScrollBar.width() > 0 && geometry.intersects(verticalScrollBar))
	{
		if (verticalScrollBar.left() == 0)
		{
			geometry.setLeft(verticalScrollBar.right());
		}
		else
		{
			geometry.setRight(verticalScrollBar.left());
		}
	}

	return geometry;
}

WindowHistoryInformation QtWebKitWebWidget::getHistory() const
{
	QVariantHash data;
	data[QLatin1String("position")] = m_webView->page()->mainFrame()->scrollPosition();
	data[QLatin1String("zoom")] = getZoom();

	m_webView->history()->currentItem().setUserData(data);

	QWebHistory *history = m_webView->history();
	WindowHistoryInformation information;
	information.index = history->currentItemIndex();

	for (int i = 0; i < history->count(); ++i)
	{
		const QWebHistoryItem item = history->itemAt(i);
		WindowHistoryEntry entry;
		entry.url = item.url().toString();
		entry.title = item.title();
		entry.position = item.userData().toHash().value(QLatin1String("position"), QPoint(0, 0)).toPoint();
		entry.zoom = item.userData().toHash().value(QLatin1String("zoom")).toInt();

		information.entries.append(entry);
	}

	return information;
}

QWebPage::WebAction QtWebKitWebWidget::mapAction(WindowAction action) const
{
	switch (action)
	{
		case OpenLinkAction:
			return QWebPage::OpenLink;
		case OpenLinkInThisTabAction:
			return QWebPage::OpenLinkInThisWindow;
		case OpenFrameInNewTabAction:
			return QWebPage::OpenFrameInNewWindow;
		case SaveLinkToDiskAction:
			return QWebPage::DownloadLinkToDisk;
		case CopyLinkToClipboardAction:
			return QWebPage::CopyLinkToClipboard;
		case OpenImageInNewTabAction:
			return QWebPage::OpenImageInNewWindow;
		case SaveImageToDiskAction:
			return QWebPage::DownloadImageToDisk;
		case CopyImageToClipboardAction:
			return QWebPage::CopyImageToClipboard;
		case CopyImageUrlToClipboardAction:
			return QWebPage::CopyImageUrlToClipboard;
		case GoBackAction:
			return QWebPage::Back;
		case GoForwardAction:
			return QWebPage::Forward;
		case StopAction:
			return QWebPage::Stop;
		case StopScheduledPageRefreshAction:
			return QWebPage::StopScheduledPageRefresh;
		case ReloadAction:
			return QWebPage::Reload;
		case ReloadAndBypassCacheAction:
			return QWebPage::ReloadAndBypassCache;
		case CutAction:
			return QWebPage::Cut;
		case CopyAction:
			return QWebPage::Copy;
		case PasteAction:
			return QWebPage::Paste;
		case DeleteAction:
			return QWebPage::DeleteEndOfWord;
		case SelectAllAction:
			return QWebPage::SelectAll;
		case UndoAction:
			return QWebPage::Undo;
		case RedoAction:
			return QWebPage::Redo;
		case InspectElementAction:
			return QWebPage::InspectElement;
		case SaveMediaToDiskAction:
			return QWebPage::DownloadMediaToDisk;
		case CopyMediaUrlToClipboardAction:
			return QWebPage::CopyMediaUrlToClipboard;
		case ToggleMediaControlsAction:
			return QWebPage::ToggleMediaControls;
		case ToggleMediaLoopAction:
			return QWebPage::ToggleMediaLoop;
		case ToggleMediaPlayPauseAction:
			return QWebPage::ToggleMediaPlayPause;
		case ToggleMediaMuteAction:
			return QWebPage::ToggleMediaMute;
		default:
			return QWebPage::NoWebAction;
	}

	return QWebPage::NoWebAction;
}

int QtWebKitWebWidget::getZoom() const
{
	return (m_webView->zoomFactor() * 100);
}

bool QtWebKitWebWidget::isLoading() const
{
	return m_isLoading;
}

bool QtWebKitWebWidget::isPrivate() const
{
	return m_webView->settings()->testAttribute(QWebSettings::PrivateBrowsingEnabled);
}

bool QtWebKitWebWidget::find(const QString &text, FindFlags flags)
{
	QWebPage::FindFlags nativeFlags = (QWebPage::FindWrapsAroundDocument | QWebPage::FindBeginsInSelection);

	if (flags & BackwardFind)
	{
		nativeFlags |= QWebPage::FindBackward;
	}

	if (flags & CaseSensitiveFind)
	{
		nativeFlags |= QWebPage::FindCaseSensitively;
	}

	if (flags & HighlightAllFind)
	{
		nativeFlags |= QWebPage::HighlightAllOccurrences;
	}

	return m_webView->findText(text, nativeFlags);
}

bool QtWebKitWebWidget::eventFilter(QObject *object, QEvent *event)
{
	if (object == m_webView)
	{
		if (event->type() == QEvent::Resize)
		{
			emit progressBarGeometryChanged();
		}
		else if (event->type() == QEvent::ToolTip && m_isLinkHovered)
		{
			event->accept();

			return true;
		}
		else if (event->type() == QEvent::MouseButtonPress)
		{
			QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

			if (mouseEvent->button() == Qt::MiddleButton)
			{
				QWebHitTestResult result = m_webView->page()->mainFrame()->hitTestContent(mouseEvent->pos());

				if (result.linkUrl().isValid())
				{
					emit requestedOpenUrl(result.linkUrl(), true, false);

					event->accept();

					return true;
				}
			}

			if (mouseEvent->button() == Qt::BackButton)
			{
				triggerAction(GoBackAction);

				event->accept();

				return true;
			}

			if (mouseEvent->button() == Qt::ForwardButton)
			{
				triggerAction(GoForwardAction);

				event->accept();

				return true;
			}
		}
		else if (event->type() == QEvent::MouseButtonDblClick && SettingsManager::getValue(QLatin1String("Browser/ShowSelectionContextMenuOnDoubleClick")).toBool())
		{
			QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

			if (mouseEvent && mouseEvent->button() == Qt::LeftButton)
			{
				QTimer::singleShot(250, this, SLOT(showContextMenu()));
			}
		}
		else if (event->type() == QEvent::Wheel)
		{
			QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);

			if (wheelEvent->modifiers() & Qt::CTRL || wheelEvent->buttons() & Qt::LeftButton)
			{
				setZoom(getZoom() + (wheelEvent->delta() / 16));

				event->accept();

				return true;
			}

		}
	}

	return QObject::eventFilter(object, event);
}

}
