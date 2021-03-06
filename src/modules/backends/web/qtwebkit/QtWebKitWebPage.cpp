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

#include "QtWebKitWebPage.h"
#include "QtWebKitWebWidget.h"
#include "../../../../core/SettingsManager.h"

#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtNetwork/QNetworkReply>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLayout>
#include <QtWidgets/QMessageBox>
#include <QtWebKit/QWebHistory>
#include <QtWebKitWidgets/QWebFrame>

namespace Otter
{

QtWebKitWebPage::QtWebKitWebPage(QtWebKitWebWidget *parent) : QWebPage(parent),
	m_webWidget(parent),
	m_ignoreJavaScriptPopups(false)
{
	optionChanged(QLatin1String("Content/ZoomTextOnly"), SettingsManager::getValue(QLatin1String("Content/ZoomTextOnly")));
	optionChanged(QLatin1String("Content/BackgroundColor"), QVariant());

	connect(this, SIGNAL(loadFinished(bool)), this, SLOT(clearIgnoreJavaScriptPopups()));
	connect(SettingsManager::getInstance(), SIGNAL(valueChanged(QString,QVariant)), this, SLOT(optionChanged(QString,QVariant)));
}

void QtWebKitWebPage::clearIgnoreJavaScriptPopups()
{
	m_ignoreJavaScriptPopups = false;
}

void QtWebKitWebPage::optionChanged(const QString &option, const QVariant &value)
{
	if (option == QLatin1String("Content/ZoomTextOnly"))
	{
		settings()->setAttribute(QWebSettings::ZoomTextOnly, value.toBool());
	}
	else if (option.startsWith(QLatin1String("Content/")))
	{
		settings()->setUserStyleSheetUrl(QUrl(QLatin1String("data:text/css;charset=utf-8;base64,") + QString(QString("html {background: %1; color: %2;} a {color: %3;} a:visited {color: %4;}").arg(SettingsManager::getValue(QLatin1String("Content/BackgroundColor")).toString()).arg(SettingsManager::getValue(QLatin1String("Content/TextColor")).toString()).arg(SettingsManager::getValue(QLatin1String("Content/LinkColor")).toString()).arg(SettingsManager::getValue(QLatin1String("Content/VisitedLinkColor")).toString()).toUtf8().toBase64())));
	}
}

void QtWebKitWebPage::javaScriptAlert(QWebFrame *frame, const QString &message)
{
	if (m_ignoreJavaScriptPopups)
	{
		return;
	}

	if (m_webWidget)
	{
		QMessageBox dialog;
		dialog.setModal(false);
		dialog.setWindowTitle(tr("JavaScript"));
		dialog.setText(message.toHtmlEscaped());
		dialog.setStandardButtons(QMessageBox::Ok);
		dialog.setCheckBox(new QCheckBox(tr("Disable JavaScript popups")));

		QEventLoop eventLoop;

		m_webWidget->showDialog(&dialog);

		connect(&dialog, SIGNAL(finished(int)), &eventLoop, SLOT(quit()));
		connect(this, SIGNAL(destroyed()), &eventLoop, SLOT(quit()));

		eventLoop.exec();

		m_webWidget->hideDialog(&dialog);

		if (dialog.checkBox()->isChecked())
		{
			m_ignoreJavaScriptPopups = true;
		}

		return;
	}

	QWebPage::javaScriptAlert(frame, message);
}

void QtWebKitWebPage::triggerAction(QWebPage::WebAction action, bool checked)
{
	if (action == InspectElement && m_webWidget)
	{
		m_webWidget->triggerAction(InspectPageAction, true);
	}

	QWebPage::triggerAction(action, checked);
}

void QtWebKitWebPage::setParent(QtWebKitWebWidget *parent)
{
	m_webWidget = parent;

	QWebPage::setParent(parent);
}

QWebPage* QtWebKitWebPage::createWindow(QWebPage::WebWindowType type)
{
	if (type == QWebPage::WebBrowserWindow)
	{
		QtWebKitWebPage *page = new QtWebKitWebPage(NULL);
		QtWebKitWebWidget *widget = new QtWebKitWebWidget(settings()->testAttribute(QWebSettings::PrivateBrowsingEnabled), NULL, page);

		if (m_webWidget)
		{
			widget->setDefaultTextEncoding(m_webWidget->getDefaultTextEncoding());
			widget->setQuickSearchEngine(m_webWidget->getSearchEngine());
			widget->setZoom(m_webWidget->getZoom());
		}

		emit requestedNewWindow(widget);

		return page;
	}

	return QWebPage::createWindow(type);
}

bool QtWebKitWebPage::acceptNavigationRequest(QWebFrame *frame, const QNetworkRequest &request, QWebPage::NavigationType type)
{
	if (request.url().scheme() == QLatin1String("javascript") && frame)
	{
		frame->evaluateJavaScript(request.url().path());

		return true;
	}

	if (type == QWebPage::NavigationTypeFormResubmitted && SettingsManager::getValue(QLatin1String("Choices/WarnFormResend")).toBool())
	{
		QMessageBox dialog;
		dialog.setWindowTitle(tr("Question"));
		dialog.setText(tr("Are you sure that you want to send form data again?"));
		dialog.setInformativeText("Do you want to resend data?");
		dialog.setIcon(QMessageBox::Question);
		dialog.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
		dialog.setDefaultButton(QMessageBox::Cancel);
		dialog.setCheckBox(new QCheckBox(tr("Do not show this message again")));

		bool cancel = false;

		if (m_webWidget)
		{
			dialog.setModal(false);

			QEventLoop eventLoop;

			m_webWidget->showDialog(&dialog);

			connect(&dialog, SIGNAL(finished(int)), &eventLoop, SLOT(quit()));
			connect(this, SIGNAL(destroyed()), &eventLoop, SLOT(quit()));

			eventLoop.exec();

			m_webWidget->hideDialog(&dialog);

			cancel = (dialog.buttonRole(dialog.clickedButton()) == QMessageBox::RejectRole);
		}
		else
		{
			cancel = (dialog.exec() == QMessageBox::Cancel);
		}

		SettingsManager::setValue(QLatin1String("Choices/WarnFormResend"), !dialog.checkBox()->isChecked());

		if (cancel)
		{
			return false;
		}
	}

	return QWebPage::acceptNavigationRequest(frame, request, type);
}

bool QtWebKitWebPage::javaScriptConfirm(QWebFrame *frame, const QString &message)
{
	if (m_ignoreJavaScriptPopups)
	{
		return false;
	}

	if (m_webWidget)
	{
		QMessageBox dialog;
		dialog.setModal(false);
		dialog.setWindowTitle(tr("JavaScript"));
		dialog.setText(message.toHtmlEscaped());
		dialog.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
		dialog.setCheckBox(new QCheckBox(tr("Disable JavaScript popups")));

		QEventLoop eventLoop;

		m_webWidget->showDialog(&dialog);

		connect(&dialog, SIGNAL(finished(int)), &eventLoop, SLOT(quit()));
		connect(this, SIGNAL(destroyed()), &eventLoop, SLOT(quit()));

		eventLoop.exec();

		m_webWidget->hideDialog(&dialog);

		if (dialog.checkBox()->isChecked())
		{
			m_ignoreJavaScriptPopups = true;
		}

		return (dialog.buttonRole(dialog.clickedButton()) == QMessageBox::AcceptRole);
	}

	return QWebPage::javaScriptConfirm(frame, message);
}

bool QtWebKitWebPage::javaScriptPrompt(QWebFrame *frame, const QString &message, const QString &defaultValue, QString *result)
{
	if (m_ignoreJavaScriptPopups)
	{
		return false;
	}

	if (m_webWidget)
	{
		QInputDialog dialog;
		dialog.setModal(false);
		dialog.setWindowTitle(tr("JavaScript"));
		dialog.setLabelText(message.toHtmlEscaped());
		dialog.setInputMode(QInputDialog::TextInput);
		dialog.setTextValue(defaultValue);

		QEventLoop eventLoop;

		m_webWidget->showDialog(&dialog);

		connect(&dialog, SIGNAL(finished(int)), &eventLoop, SLOT(quit()));
		connect(this, SIGNAL(destroyed()), &eventLoop, SLOT(quit()));

		eventLoop.exec();

		m_webWidget->hideDialog(&dialog);

		if (dialog.result() == QDialog::Accepted)
		{
			*result = dialog.textValue();
		}

		return (dialog.result() == QDialog::Accepted);
	}

	return QWebPage::javaScriptPrompt(frame, message, defaultValue, result);
}

bool QtWebKitWebPage::extension(QWebPage::Extension extension, const QWebPage::ExtensionOption *option, QWebPage::ExtensionReturn *output)
{
	if (extension == QWebPage::ErrorPageExtension)
	{
		const QWebPage::ErrorPageExtensionOption *errorOption = static_cast<const QWebPage::ErrorPageExtensionOption*>(option);
		QWebPage::ErrorPageExtensionReturn *errorOutput = static_cast<QWebPage::ErrorPageExtensionReturn*>(output);

		if (!errorOption || !errorOutput)
		{
			return false;
		}

		QFile file(QLatin1String(":/files/error.html"));
		file.open(QIODevice::ReadOnly | QIODevice::Text);

		QTextStream stream(&file);
		stream.setCodec("UTF-8");

		QHash<QString, QString> variables;
		variables[QLatin1String("title")] = tr("Error %1").arg(errorOption->error);
		variables[QLatin1String("description")] = errorOption->errorString;
		variables[QLatin1String("dir")] = (QGuiApplication::isLeftToRight() ? QLatin1String("ltr") : QLatin1String("rtl"));

		QString html = stream.readAll();
		QHash<QString, QString>::iterator iterator;

		for (iterator = variables.begin(); iterator != variables.end(); ++iterator)
		{
			html.replace(QString("{%1}").arg(iterator.key()), iterator.value());
		}

		errorOutput->content = html.toUtf8();

		return true;
	}

	return false;
}

bool QtWebKitWebPage::shouldInterruptJavaScript()
{
	if (m_webWidget)
	{
		QMessageBox dialog;
		dialog.setModal(false);
		dialog.setWindowTitle(tr("Question"));
		dialog.setText(tr("The script on this page appears to have a problem."));
		dialog.setInformativeText(tr("Do you want to stop the script?"));
		dialog.setIcon(QMessageBox::Question);
		dialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No);

		QEventLoop eventLoop;

		m_webWidget->showDialog(&dialog);

		connect(&dialog, SIGNAL(finished(int)), &eventLoop, SLOT(quit()));
		connect(this, SIGNAL(destroyed()), &eventLoop, SLOT(quit()));

		eventLoop.exec();

		m_webWidget->hideDialog(&dialog);

		return (dialog.buttonRole(dialog.clickedButton()) == QMessageBox::YesRole);
	}

	return QWebPage::shouldInterruptJavaScript();
}

bool QtWebKitWebPage::supportsExtension(QWebPage::Extension extension) const
{
	return (extension == QWebPage::ErrorPageExtension);
}

}
