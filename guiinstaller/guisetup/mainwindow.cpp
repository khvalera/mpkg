#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QCheckBox>
#include <QMessageBox>
#include <QSettings>
#include <QListWidgetItem>
#include "help.h"
#include "mount.h"
MainWindow *guiObject;


MpkgErrorReturn qtErrorHandler(ErrorDescription err, const string& details) {
	return guiObject->errorHandler(err, details);
}

MpkgErrorReturn MainWindow::errorHandler(ErrorDescription err, const string& details) {
	QMessageBox box(this);
	QVector<QPushButton *> buttons;
	if (err.action.size()>1) {
		for (size_t i=0; i<err.action.size(); ++i) {
			buttons.push_back(box.addButton(err.action[i].text.c_str(), QMessageBox::AcceptRole));
		}
	}
	box.setWindowTitle(tr("Error"));
	box.setText(err.text.c_str());
	box.setInformativeText(details.c_str());
	box.exec();
	if (err.action.size()==1) return err.action[0].ret;
	for (int i=0; i<buttons.size(); ++i) {
		if (box.clickedButton()==buttons[i]) return err.action[i].ret;
	}
	return MPKG_RETURN_ABORT;
}

MountOptions::MountOptions(QTreeWidgetItem *_item, QString _partition, uint64_t _psize, QString _size, QString _currentfs, QString _mountpoint, bool _format, QString _newfs) {
	itemPtr = _item;
	partition = _partition;
	psize = _psize;
	size = _size;
	currentfs = _currentfs;
	mountpoint = _mountpoint;
	format = _format;
	newfs = _newfs;
	printf("Creating object with data: %s %s %d %s\n", partition.toStdString().c_str(), mountpoint.toStdString().c_str(), format, newfs.toStdString().c_str());
	setupMode = true;
}

MountOptions::~MountOptions() {
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindowClass) {
	hasNvidia = -1;
	mpkgErrorHandler.registerErrorHandler(qtErrorHandler);
	guiObject = this;
	ui->setupUi(this);
	ui->releaseNotesTextBrowser->hide();
	setWindowState(Qt::WindowMaximized);
	connect(ui->nextButton, SIGNAL(clicked()), this, SLOT(nextButtonClick()));
	connect(ui->backButton, SIGNAL(clicked()), this, SLOT(backButtonClick()));
	connect(ui->mountPointsTreeWidget, SIGNAL(currentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *)), this, SLOT(updateMountsGUI(QTreeWidgetItem *, QTreeWidgetItem *)));
	connect(ui->installButton, SIGNAL(clicked()), this, SLOT(runInstaller()));

	connect(ui->mountDoFormatRadioButton, SIGNAL(toggled(bool)), this, SLOT(updateMountItemUI()));
	connect(ui->mountDontUseRadioButton, SIGNAL(toggled(bool)), this, SLOT(updateMountItemUI()));
	connect(ui->mountRootRadioButton, SIGNAL(toggled(bool)), this, SLOT(updateMountItemUI()));
	connect(ui->mountSwapRadioButton, SIGNAL(toggled(bool)), this, SLOT(updateMountItemUI()));
	connect(ui->mountNoFormatRadioButton, SIGNAL(toggled(bool)), this, SLOT(updateMountItemUI()));
	connect(ui->mountCustomRadioButton, SIGNAL(toggled(bool)), this, SLOT(updateMountItemUI()));
	connect(ui->mountFormatOptionsComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(updateMountItemUI()));
	connect(ui->mountPointEdit, SIGNAL(textEdited(const QString &)), this, SLOT(updateMountItemUI()));
	connect(ui->releaseNotesButton, SIGNAL(clicked()), this, SLOT(showHideReleaseNotes()));
	connect(ui->helpButton, SIGNAL(clicked()), this, SLOT(showHelp()));
	connect(ui->saveConfigButton, SIGNAL(clicked()), this, SLOT(saveConfigAndExit()));
	lockUIUpdate = false;
	connect(ui->timezoneSearchEdit, SIGNAL(textEdited(const QString &)), this, SLOT(timezoneSearch(const QString &)));
	ui->stackedWidget->setCurrentIndex(0);
	settings = new QSettings("guiinstaller");
	loadSetupVariantsThread = new LoadSetupVariantsThread;
	qRegisterMetaType< vector<CustomPkgSet> > ("vector<CustomPkgSet>");
	connect(loadSetupVariantsThread, SIGNAL(finished(bool, const vector<CustomPkgSet> &)), this, SLOT(receiveLoadSetupVariants(bool, const vector<CustomPkgSet> &)));
	connect(loadSetupVariantsThread, SIGNAL(sendLoadText(const QString &)), this, SLOT(getLoadText(const QString &)));
	connect(loadSetupVariantsThread, SIGNAL(sendLoadProgress(int)), this, SLOT(getLoadProgress(int)));
	connect(ui->quitButton, SIGNAL(clicked()), this, SLOT(askQuit()));

	// Setup variants handling
	connect(ui->setupVariantsListWidget, SIGNAL(currentRowChanged(int)), this, SLOT(showSetupVariantDescription(int)));

	// Mountpoints action filtering
	connect(ui->mountDontUseRadioButton, SIGNAL(toggled(bool)), this, SLOT(mountFilterDontUse(bool)));
	connect(ui->mountRootRadioButton, SIGNAL(toggled(bool)), this, SLOT(mountFilterRoot(bool)));
	connect(ui->mountSwapRadioButton, SIGNAL(toggled(bool)), this, SLOT(mountFilterSwap(bool)));
	connect(ui->mountCustomRadioButton, SIGNAL(toggled(bool)), this, SLOT(mountFilterCustom(bool)));
	connect(ui->mountNoFormatRadioButton, SIGNAL(toggled(bool)), this, SLOT(mountFilterNoFormat(bool)));

}

MainWindow::~MainWindow() {
	delete settings;
	delete loadSetupVariantsThread;
}
void MainWindow::closeEvent(QCloseEvent *event) {
	if (QMessageBox::question(this, tr("Really cancel installation?"), 
				tr("Installation is not yet complete. Do you really want to cancel?"), 
				QMessageBox::Yes|QMessageBox::No)==QMessageBox::Yes) {
		event->accept();
	}
	else event->ignore();
}

void MainWindow::showHelp() {
	QString text = ReadFile("/usr/local/share/setup/help/" + IntToStr(ui->stackedWidget->currentIndex()) + ".html").c_str();
	if (text.isEmpty()) {
		QMessageBox::information(this, tr("No help available"), tr("Sorry, no help available for this part"));
		return;
	}
	HelpForm *helpForm = new HelpForm;
	helpForm->loadText(text);
	helpForm->show();

}

void MainWindow::askQuit() {
	if (QMessageBox::question(this, tr("Really cancel installation?"), tr("Installation is not yet complete. Do you really want to cancel?"), QMessageBox::Yes|QMessageBox::No)==QMessageBox::Yes) qApp->quit();
}
void MainWindow::runInstaller() {
	if (!ui->confirmInstallCheckBox->isChecked()) {
		QMessageBox::critical(this, tr("Please confirm settings"), tr("You chould confirm that settings are OK. Check the appropriate check box."));
		return;
	}
	else settings->setValue("anonstat", false);
	unlink("/var/run/guisetup.pid"); // need to unlock before starting next process
	system("LC_ALL=" + settings->value("language").toString().toStdString() + " nohup guisetup_exec 2>&1 >/var/log/guisetup_exec.log &");
	qApp->quit();
}

void MainWindow::saveConfigAndExit() {
	QMessageBox::information(this, tr("Configuration saved"), tr("Setup configuration saved to /root/.config/guiinstaller.conf\nYou can store it and use in next installations."));
	qApp->quit();
}

void MainWindow::nextButtonClick() {
	if (!validatePageSettings(ui->stackedWidget->currentIndex())) return;
	int shift = 1;
	while (!checkLoad(ui->stackedWidget->currentIndex()+shift)) shift++;
	storePageSettings(ui->stackedWidget->currentIndex());
	if (ui->stackedWidget->currentIndex()==ui->stackedWidget->count()-1-shift) ui->nextButton->setEnabled(false);
	else if (ui->stackedWidget->currentIndex()==0) ui->backButton->setEnabled(true);
	ui->stackedWidget->setCurrentIndex(ui->stackedWidget->currentIndex()+shift);
	ui->statusbar->showMessage(tr("Step %1 of %2").arg(ui->stackedWidget->currentIndex()).arg(ui->stackedWidget->count()-1));
	updatePageData(ui->stackedWidget->currentIndex());
}

void MainWindow::backButtonClick() {
	if (ui->stackedWidget->currentIndex()==1) ui->backButton->setEnabled(false);
	else if (ui->stackedWidget->currentIndex()==ui->stackedWidget->count()-1) ui->nextButton->setEnabled(true);
	int shift = 1;
	if (ui->stackedWidget->currentIndex()==PAGE_INSTALLTYPE) shift=2;
	while (!checkLoad(ui->stackedWidget->currentIndex()-shift)) shift--;
	ui->stackedWidget->setCurrentIndex(ui->stackedWidget->currentIndex()-shift);
	ui->statusbar->showMessage(tr("Step %1 of %2").arg(ui->stackedWidget->currentIndex()).arg(ui->stackedWidget->count()-1));
	//updatePageData(ui->stackedWidget->currentIndex());

}

bool MainWindow::validatePageSettings(int index) {
	switch(index) {
		case PAGE_LICENSE:
			if (!ui->licenseAcceptCheckBox->isChecked()) {
				QMessageBox::critical(this, tr("License not accepted"), tr("Without accepting license, you cannot proceed."));
				return false;
			}
			break;
		case PAGE_PARTITIONING:
			if (ui->partitioningManualRadioButton->isChecked()) runPartitioningTool();
			updatePartitionLists();
			if (drives.empty()) {
				QMessageBox::critical(this, tr("No hard drives detected"), tr("Installer cannot detect any hard drives in your computer. It may be caused by:\n - You really didn't have any hard drives,\n - Sort of hardware problems (check cables first),\n - Invalid BIOS configuration (check that your drive controllers is enabled),\n - currently unsupported hardware (in this case, please report to developers).\n\nInstallation cannot continue and will exit now."));
				qApp->quit();
			}
			if (partitions.empty()) {
				QMessageBox::critical(this, tr("No partitions detected"), tr("You have no partitions on your hard drive. Please create it first."));
				return false;
			}
			break;
		case PAGE_MOUNTPOINTS:
			if (!validateMountPoints()) return false;
			break;
		case PAGE_BOOTLOADER:
			if (!validateBootloaderSettings()) return false;
			break;
		case PAGE_PKGSOURCE:
			if (!validatePkgSources()) return false;
			break;
		case PAGE_ROOTPASSWORD:
			if (!validateRootPassword()) return false;
			break;
		case PAGE_USERS:
			if (!validateUserPasswords()) return false;
			break;
		case PAGE_TIMEZONE:
			if (!ui->timezoneListWidget->currentItem()) {
				QMessageBox::critical(this, tr("Timezone not selected"), tr("Please select timezone"));
				return false;
			}
			break;
		default:
			break;
	}
	return true;


}

void MainWindow::storePageSettings(int index) {
	switch(index) {
		case PAGE_LICENSE:
			settings->setValue("license_accepted", true);
			break;
		case PAGE_PARTITIONING:
			break;
		case PAGE_MOUNTPOINTS:
			saveMountSettings();
			break;
		case PAGE_BOOTLOADER:
			saveBootloaderSettings();
			break;
		case PAGE_PKGSOURCE:
			savePkgsourceSettings();
			break;
		case PAGE_INSTALLTYPE:
			saveSetupVariant();
			break;
		case PAGE_ALTERNATIVES:
			saveAlternatives();
			break;
		case PAGE_NVIDIA:
			saveNvidia();
			break;
		case PAGE_TIMEZONE:
			saveTimezone();
			break;
		case PAGE_ROOTPASSWORD:
			saveRootPassword();
			break;
		case PAGE_NETWORKING:
			saveNetworking();
			break;
		case PAGE_USERS:
			saveUsers();
			break;
		default:
			break;
	}
	settings->sync();

}

void MainWindow::updatePageData(int index) {
	switch(index) {
		case PAGE_LICENSE:
			loadLicense();
			break;
		case PAGE_PARTITIONING:
			loadPartitioningDriveList();
			break;
		case PAGE_MOUNTPOINTS:
			loadMountsTree();
			break;
		case PAGE_BOOTLOADER:
			loadBootloaderTree();
			break;
		case PAGE_PKGSOURCE:
			break;
		case PAGE_WAITPKGSOURCE:
			loadSetupVariants();
			break;
		case PAGE_TIMEZONE:
			loadTimezones();
			break;
		case PAGE_CONFIRMATION:
			loadConfirmationData();
		default:
			break;
	}
	settings->sync();
}

void MainWindow::loadLicense() {
	QString lang = settings->value("language").toString();
	QString license = ReadFile("/usr/local/share/setup/licenses/license." + lang.toStdString()).c_str();
	ui->licenseBrowser->setPlainText(license);
}

/*string getUUID(const string& dev) {
	string tmp_file = get_tmp_file();
	string data;
	int try_count = 0;
	while (try_count<2 && data.empty()) {
		system("blkid -s UUID " + dev + " > " + tmp_file);
		data = ReadFile(tmp_file);
		try_count++;
	}
	if (data.empty()) return "";
	size_t a = data.find_first_of("\"");
	if (a==std::string::npos || a==data.size()-1) return "";
	data.erase(data.begin(), data.begin()+a+1);
	a = data.find_first_of("\"");
	if (a==std::string::npos || a==0) return "";
	return data.substr(0, a);
}
*/
string getLABEL(const string& dev) {
	string tmp_file = get_tmp_file();
	string data;
	int try_count = 0;
	while (try_count<2 && data.empty()) {
		system("blkid -s LABEL " + dev + " > " + tmp_file);
		data = ReadFile(tmp_file);
		try_count++;
	}
	if (data.empty()) return "";
	size_t a = data.find_first_of("\"");
	if (a==std::string::npos || a==data.size()-1) return "";
	data.erase(data.begin(), data.begin()+a+1);
	a = data.find_first_of("\"");
	if (a==std::string::npos || a==0) return "";
	return data.substr(0, a);
}
void MainWindow::updatePartitionLists() {
	drives = getDevList();
	partitions = getPartitionList();
	lvm_groups = getLVM_VGList();
	// Since fslabel in libparted means completely other thing, let's get this from blkid
	for (size_t i=0; i<partitions.size(); ++i) {
		partitions[i].fslabel = getLABEL(partitions[i].devname).c_str();
	}
}

void MainWindow::loadPartitioningDriveList() {
	updatePartitionLists();
	/*ui->autoPartitionDriveComboBox->clear();
	for (size_t i=0; i<drives.size(); ++i) {
		ui->autoPartitionDriveComboBox->addItem(QString("%1 (%2)").arg(QString::fromStdString(drives[i].tag)).arg(QString::fromStdString(drives[i].value)));
	}
	for (size_t i=0; i<lvm_groups.size(); ++i) {
		ui->autoPartitionDriveComboBox->addItem(QString("%1 (%2)").arg(QString::fromStdString(lvm_groups[i].name)).arg(QString::fromStdString(lvm_groups[i].size)));
	}*/

}


void MainWindow::runPartitioningTool() {
	Qt::WindowStates winstate = windowState();
	setWindowState(Qt::WindowMinimized);
	system("gparted");
	setWindowState(Qt::WindowMaximized);
	deviceCacheActual = false;
	partitionCacheActual = false;
	updatePartitionLists();
}

void MainWindow::loadMountsTree() {
	updatePartitionLists();
	ui->mountPointsTreeWidget->clear();
	mountOptions.clear();
	QString fslabel;
	
	// First - physical drives
	for (size_t i=0; i<drives.size(); ++i) {
		QTreeWidgetItem *coreItem = new QTreeWidgetItem(ui->mountPointsTreeWidget, QStringList(QString("%1 (%2)").arg(QString::fromStdString(drives[i].tag)).arg(QString::fromStdString(drives[i].value))));
		coreItem->setExpanded(true);
		for (size_t p=0; p<partitions.size(); ++p) {
			if (partitions[p].devname.find(drives[i].tag)!=0) continue;
			fslabel = partitions[p].fslabel.c_str();
			if (fslabel.isEmpty()) fslabel = tr(", no label");
			else fslabel=tr(", label: %1").arg(partitions[p].fslabel.c_str());
			QTreeWidgetItem *partitionItem = new QTreeWidgetItem(coreItem, QStringList(QString("%1%2 (%3, %4)").arg(QString::fromStdString(partitions[p].devname)).arg(fslabel).arg(QString::fromStdString(humanizeSize((int64_t) atol(partitions[p].size.c_str())*(int64_t) 1048576))).arg(QString::fromStdString(partitions[p].fstype))));
			mountOptions.push_back(MountOptions(partitionItem, QString::fromStdString(partitions[p].devname), (int64_t) atol(partitions[p].size.c_str())*(int64_t) 1048576, QString::fromStdString(humanizeSize((int64_t) atol(partitions[p].size.c_str())*(int64_t) 1048576)),QString::fromStdString(partitions[p].fstype)));
		}
	}
	// Now - LVM
	for (size_t i=0; i<lvm_groups.size(); ++i) {
		QTreeWidgetItem *coreItem = new QTreeWidgetItem(ui->mountPointsTreeWidget, QStringList(QString("LVM Volume Group %1 (%2)").arg(QString::fromStdString(lvm_groups[i].name)).arg(QString::fromStdString(lvm_groups[i].size))));
		coreItem->setExpanded(true);
		for (size_t p=0; p<partitions.size(); ++p) {
			if (partitions[p].devname.find("/dev/"+lvm_groups[i].name + "/")!=0) continue;
			fslabel = partitions[p].fslabel.c_str();
			if (fslabel.isEmpty()) fslabel = tr(", no label");
			else fslabel=tr(", label: %1").arg(partitions[p].fslabel.c_str());
			QTreeWidgetItem *partitionItem = new QTreeWidgetItem(coreItem, QStringList(QString("%1%2 (%3, %4)").arg(QString::fromStdString(partitions[p].devname)).arg(fslabel).arg(QString::fromStdString(humanizeSize((int64_t) atol(partitions[p].size.c_str())*(int64_t) 1048576))).arg(QString::fromStdString(partitions[p].fstype))));
			mountOptions.push_back(MountOptions(partitionItem, QString::fromStdString(partitions[p].devname), (int64_t) atol(partitions[p].size.c_str())*(int64_t) 1048576, QString::fromStdString(humanizeSize((int64_t) atol(partitions[p].size.c_str())*(int64_t) 1048576)),QString::fromStdString(partitions[p].fstype)));
		}
	}

}

void MainWindow::updateMountItemUI() {
	if (lockUIUpdate) return;
	QTreeWidgetItem *item = ui->mountPointsTreeWidget->currentItem();
	if (!item) return;
	MountOptions *mountPtr = NULL;
	for (size_t i=0; i<mountOptions.size(); ++i) {
		if (item == mountOptions[i].itemPtr) {
			mountPtr = &mountOptions[i];
			break;
		}
	}
	if (!mountPtr) return;

	QString newdata, formatdata;
	processMountEdit(item);
	newdata.clear();
	formatdata.clear();
	if (!mountPtr->mountpoint.isEmpty()) newdata = ": " + mountPtr->mountpoint;
	if (mountPtr->format) formatdata = tr(", format to: %1").arg(mountPtr->newfs);
	item->setText(0, QString("%1 (%2, %3)%4%5").arg(mountPtr->partition).arg(mountPtr->size).arg(mountPtr->currentfs).arg(newdata).arg(formatdata));
	if (mountPtr->mountpoint=="/") item->setBackground(0, QBrush(QColor(186,15,88,127)));
	else if (mountPtr->mountpoint=="swap") item->setBackground(0, QBrush(QColor(215,91,24,127)));
	else if (mountPtr->mountpoint.isEmpty()) item->setBackground(0, QBrush(QColor(255,255,255,0)));
	else if (mountPtr->format) item->setBackground(0, QBrush(QColor(59,74,215,127)));
	else item->setBackground(0, QBrush(QColor(255,0,0,127)));

}

void MainWindow::updateMountsGUI(QTreeWidgetItem *nextItem, QTreeWidgetItem *prevItem) {
	lockUIUpdate = true;
	// Let's debug and print whole table
	MountOptions *prevMountPtr = NULL, *nextMountPtr = NULL;
	for (size_t i=0; i<mountOptions.size(); ++i) {
		if (nextItem == mountOptions[i].itemPtr) {
			nextMountPtr = &mountOptions[i];
			break;
		}
	}
	for (size_t i=0; i<mountOptions.size(); ++i) {
		if (prevItem == mountOptions[i].itemPtr) {
			prevMountPtr = &mountOptions[i];
			break;
		}
	}
	ui->mountOptionsGroupBox->setEnabled(true);
	ui->formatOptionsGroupBox->setEnabled(true);


	QString newdata, formatdata;
	if (prevMountPtr && prevItem) {
		processMountEdit(prevItem);
		newdata.clear();
		formatdata.clear();
		if (!prevMountPtr->mountpoint.isEmpty()) newdata = ": " + prevMountPtr->mountpoint;
		if (prevMountPtr->format) formatdata = tr(", format to: %1").arg(prevMountPtr->newfs);
		if (!prevMountPtr->mount_options.isEmpty()) formatdata += tr(", options: %1").arg(prevMountPtr->mount_options);

		prevItem->setText(0, QString("%1 (%2, %3)%4%5").arg(prevMountPtr->partition).arg(prevMountPtr->size).arg(prevMountPtr->currentfs).arg(newdata).arg(formatdata));
	}
	
	if (nextItem && nextMountPtr) {
		ui->mountPointEdit->setText("");
		if (nextMountPtr->mountpoint=="/") ui->mountRootRadioButton->setChecked(true);
		else if (nextMountPtr->mountpoint.isEmpty()) ui->mountDontUseRadioButton->setChecked(true);
		else if (nextMountPtr->mountpoint=="swap") ui->mountSwapRadioButton->setChecked(true);
		else {
			ui->mountCustomRadioButton->setChecked(true);
			ui->mountPointEdit->setText(nextMountPtr->mountpoint);
		}
		ui->mountOptionsLineEdit->setText(nextMountPtr->mount_options);


		int itemID;
		if (nextMountPtr->format) {
			ui->mountDoFormatRadioButton->setChecked(true);
			itemID = ui->mountFormatOptionsComboBox->findText(nextMountPtr->newfs);
			if (itemID!=-1) ui->mountFormatOptionsComboBox->setCurrentIndex(itemID);
		}
		else {
			ui->mountNoFormatRadioButton->setChecked(true);
		}
	}
	else {
		ui->mountOptionsGroupBox->setEnabled(false);
		ui->formatOptionsGroupBox->setEnabled(false);
	}
	lockUIUpdate = false;
}

void MainWindow::processMountEdit(QTreeWidgetItem *item) {
	if (!item) {
		printf("processMountEdit: NULL ITEM!\n");
		return;
	}
	MountOptions *mountPtr = NULL;
	for (size_t i=0; i<mountOptions.size(); ++i) {
		if (item == mountOptions[i].itemPtr) {
			mountPtr = &mountOptions[i];
			break;
		}
	}
	if (mountPtr==NULL) return;

	if (ui->mountRootRadioButton->isChecked()) mountPtr->mountpoint = "/";
	else if (ui->mountSwapRadioButton->isChecked()) mountPtr->mountpoint = "swap";
	else if (ui->mountDontUseRadioButton->isChecked()) mountPtr->mountpoint.clear();
	else if (ui->mountCustomRadioButton->isChecked()) mountPtr->mountpoint=ui->mountPointEdit->text();
	mountPtr->mount_options=ui->mountOptionsLineEdit->text();

	if (ui->mountNoFormatRadioButton->isChecked()) {
		mountPtr->format=false;
		mountPtr->newfs.clear();
	}
	else {
		mountPtr->format=true;
		mountPtr->newfs=ui->mountFormatOptionsComboBox->currentText();
	}
}

void MainWindow::saveMountSettings() {
	processMountEdit(ui->mountPointsTreeWidget->currentItem());
	settings->beginGroup("mount_options");
	settings->remove("");
	for (size_t i=0; i<mountOptions.size(); ++i) {
		if (mountOptions[i].mountpoint.isEmpty() && !mountOptions[i].format) continue;
		settings->beginGroup(mountOptions[i].partition.toStdString().substr(5).c_str());
		settings->setValue("mountpoint", mountOptions[i].mountpoint);
		settings->setValue("format", mountOptions[i].format);
		settings->setValue("options", mountOptions[i].mount_options);
		if (mountOptions[i].format) settings->setValue("fs", mountOptions[i].newfs);
		else settings->setValue("fs", mountOptions[i].currentfs);
		settings->endGroup();
	}
	settings->endGroup();
}

void MainWindow::loadBootloaderTree() {
	updatePartitionLists();
	ui->bootLoaderComboBox->clear();
	ui->bootPartitionComboBox->clear();
	
	// Physical drives
	for (size_t i=0; i<drives.size(); ++i) {
		ui->bootLoaderComboBox->addItem(QString("%1 (%2)").arg(QString::fromStdString(drives[i].tag)).arg(QString::fromStdString(drives[i].value)));
	}

	// Bootable partitions
	
	for (size_t i=0; i<partitions.size(); ++i) {
		ui->bootPartitionComboBox->addItem(QString("%1 (%2)").arg(QString::fromStdString(partitions[i].devname)).arg(QString::fromStdString(partitions[i].size + " Mb")));
	}

	ui->bootLoaderComboBox->addItem(tr("No bootloader"));
}

bool MainWindow::validateBootloaderSettings() {
	if (ui->bootLoaderComboBox->currentIndex()==-1) {
		QMessageBox::critical(this, tr("No bootloader disk selected"), tr("You should specify drive or partition where bootloader will be installed."));
		return false;
	}
	return true;
}

void MainWindow::saveBootloaderSettings() {
	QString fbmode = ui->fbResolutionComboBox->currentText();
	if (fbmode=="KMS" || fbmode == "Kernel modesetting") fbmode = "text";
	if (fbmode!="text") fbmode += "x" + ui->fbColorComboBox->currentText();
	if (ui->bootDeviceRadioButton->isChecked()) {
		if (ui->bootLoaderComboBox->currentIndex()<0) return;
		if (ui->bootLoaderComboBox->currentIndex()==ui->bootLoaderComboBox->count()-1) settings->setValue("bootloader", "NONE");
		else settings->setValue("bootloader", drives[ui->bootLoaderComboBox->currentIndex()].tag.c_str());
	}
	else {
		if (ui->bootPartitionComboBox->currentIndex()<0) return;
		settings->setValue("bootloader", partitions[ui->bootPartitionComboBox->currentIndex()].devname.c_str());
	}
	settings->setValue("fbmode", fbmode);
	settings->setValue("initrd_delay", ui->initrdDelayCheckBox->isChecked());
	settings->setValue("kernel_options", ui->kernelOptionsLineEdit->text());
	settings->setValue("initrd_modules", ui->initrdModulesLineEdit->text());
	settings->setValue("tmpfs_tmp", ui->useTmpfsCheckBox->isChecked());
	
}

bool MainWindow::validatePkgSources() {
	if (ui->pkgSourceISORadioButton->isChecked()) {
		QString defaultPath = "/media";
		if (QMessageBox::question(this, tr("Partition mount"), tr("Do you want to mount partition with packages?"), QMessageBox::Yes|QMessageBox::No)==QMessageBox::Yes) {
			MountWidget mountWidget;
			updatePartitionLists();
			mountWidget.init(&partitions);
			mountWidget.setWindowTitle(tr("Select partition with ISO image"));
			if (mountWidget.exec()) defaultPath = mountWidget.selectedMountPoint;
			qDebug() << defaultPath << endl;
		}

		isopath = QFileDialog::getOpenFileName(this, tr("Specify ISO image"), defaultPath, tr("ISO image (*.iso)"), 0, QFileDialog::DontUseNativeDialog);
		if (isopath.isEmpty() || !FileExists(isopath.toStdString())) {
			return false;
		}
		printf("Got ISO image in %s\n", isopath.toStdString().c_str());
	}
	else if (ui->pkgSourceHDDRadioButton->isChecked()) {
		QString defaultPath = "/media";

		if (QMessageBox::question(this, tr("Partition mount"), tr("Do you want to mount partition with packages?"), QMessageBox::Yes|QMessageBox::No)==QMessageBox::Yes) {
			MountWidget mountWidget;
			updatePartitionLists();
			mountWidget.init(&partitions);
			mountWidget.setWindowTitle(tr("Select partition with packages"));
			if (mountWidget.exec()) defaultPath = mountWidget.selectedMountPoint;
			qDebug() << defaultPath << endl;
		}
		hddpath = QFileDialog::getExistingDirectory(this, tr("Specify directory with packages"), defaultPath, QFileDialog::DontUseNativeDialog);
		if (hddpath.isEmpty() || !FileExists(hddpath.toStdString())) {
			return false;
		}
		if (!FileExists(hddpath.toStdString() + "/packages.xml.gz")) {
			QMessageBox::information(this, tr("Incorrect directory"), tr("Specified directory does not contain repository index."));
			return false;
		}
		printf("Got HDD dir in %s\n", hddpath.toStdString().c_str());
	}
	else if (ui->pkgSourceCustomRadioButton->isChecked()) {
		urlpath = QInputDialog::getText(this, tr("Specify repository URL"), tr("Please, enter repository URL, for example, http://core32.agilialinux.ru/"));
		if (urlpath.isEmpty()) return false;
	}

	return true;
}
void MainWindow::savePkgsourceSettings() {
	if (ui->pkgSourceDVDRadioButton->isChecked()) {
		settings->setValue("pkgsource", "dvd");
	}
	else if (ui->pkgSourceISORadioButton->isChecked()) {
		settings->setValue("pkgsource", QString("iso://%1").arg(isopath));
	}
	else if (ui->pkgSourceHDDRadioButton->isChecked()) {
		settings->setValue("pkgsource", QString("file://%1/").arg(hddpath));
	}
	else if (ui->pkgSourceCustomRadioButton->isChecked()) {
		settings->setValue("pkgsource", urlpath);
	}
	else if (ui->pkgSourceNetworkRadioButton->isChecked()) {
#ifdef X86_64
		settings->setValue("pkgsource", "http://core64.agilialinux.ru/");
#else
		settings->setValue("pkgsource", "http://core32.agilialinux.ru/");
#endif
	}
}


void MainWindow::getLoadText(const QString &_text) {
	ui->repTextLabel->setText(_text);
}

void MainWindow::getLoadProgress(int _r_v) {
	ui->repProgressBar->setValue(_r_v);
}



void MainWindow::loadSetupVariants() {
	ui->repTextLabel->setText("");
	ui->repProgressBar->setValue(0);
	// Let's write down repo list
	if (loadSetupVariantsThread->isRunning()) {
		if (!loadSetupVariantsThread->wait(5000)) loadSetupVariantsThread->terminate();
	}
	loadSetupVariantsThread->pkgsource = settings->value("pkgsource").toString();
	loadSetupVariantsThread->locale = settings->value("language").toString().toStdString();
	
	loadSetupVariantsThread->start();
	ui->nextButton->setEnabled(false);
	ui->backButton->setEnabled(false);
}

void MainWindow::showSetupVariantDescription(int index) {
	if (index<0 || index>=(int) customPkgSetList.size()) {
		ui->setupVariantDescription->clear();
		return;
	}
	ui->setupVariantDescription->setText(tr("<p><b>Description:</b> %1</p><p><b>Packages to install:</b> %2 (%3)</p><p><b>Disk space required:</b> %4</p><p>Please note that space requirement is estimated very approximately and does not respect partitioning scheme, temporary files and required space for work.</p>").arg(customPkgSetList[index].full.c_str()).arg(customPkgSetList[index].count).arg(humanizeSize(customPkgSetList[index].csize).c_str()).arg(humanizeSize(customPkgSetList[index].isize).c_str()));
}

void MainWindow::receiveLoadSetupVariants(bool success, const vector<CustomPkgSet> &_pkgSet) {
	if (!success) {
		if (settings->value("pkgsource")=="dvd") {
			if (QMessageBox::question(this, tr("DVD detection failed"), tr("Failed to detect DVD drive. Be sure you inserted installation DVD into this. Retry?"), QMessageBox::Yes|QMessageBox::No)==QMessageBox::Yes) {
				backButtonClick();
				return;
			}
			else qApp->quit();
		}
		else {
			if (QMessageBox::question(this, tr("Repository connection failed"), tr("Failed to connect to repository. If you trying to access network repository, check your network settings. Retry?"), QMessageBox::Yes|QMessageBox::No)==QMessageBox::Yes) {
				backButtonClick();
				return;
			}
			else qApp->quit();


		}
	}

	customPkgSetList = _pkgSet;
	settings->setValue("pkgsource", loadSetupVariantsThread->pkgsource);

	printf("Received %d items in pkgSetList\n", customPkgSetList.size());
	settings->setValue("volname", loadSetupVariantsThread->volname.c_str());
	settings->setValue("rep_location", loadSetupVariantsThread->rep_location.c_str());
	
	ui->setupVariantsListWidget->clear();
	QListWidgetItem *item;
	for (size_t i=0; i<customPkgSetList.size(); ++i) {
		item = new QListWidgetItem(customPkgSetList[i].desc.c_str(), ui->setupVariantsListWidget);
	}

	ui->nextButton->setEnabled(true);
	ui->backButton->setEnabled(true);
	nextButtonClick();
}

void MainWindow::loadTimezones() {
	string tmpfile = get_tmp_file();
	system("( cd /usr/share/zoneinfo && find . -type f | sed -e 's/\\.\\///g') > " + tmpfile);
	vector<string> tz = ReadFileStrings(tmpfile);
	unlink(tmpfile.c_str());
	ui->timezoneListWidget->clear();
	QListWidgetItem *item;
	for (size_t i=0; i<tz.size(); ++i) {
		item = new QListWidgetItem(tz[i].c_str(), ui->timezoneListWidget);
	}
}

void MainWindow::saveSetupVariant() {
	int index = ui->setupVariantsListWidget->currentRow();
	if (index!=-1) settings->setValue("setup_variant", customPkgSetList[index].name.c_str());
}

void MainWindow::saveTimezone() {
	settings->setValue("timezone", ui->timezoneListWidget->currentItem()->text());
	if (ui->utcCheckBox->isChecked()) settings->setValue("time_utc", true);
	else settings->setValue("time_utc", false);
}

void MainWindow::loadConfirmationData() {
	QString newdata, formatdata;
	
	QString formatted, notFormatted;
	MountOptions *mountPtr = NULL;
	for (size_t i=0; i<mountOptions.size(); ++i) {
		mountPtr = &mountOptions[i];
		newdata.clear();
		formatdata.clear();
		if (!mountPtr->mountpoint.isEmpty()) newdata = ": " + mountPtr->mountpoint;
		if (mountPtr->format) {
			formatdata = tr(", format to: %1").arg(mountPtr->newfs);
			formatted += QString("<li><u>%1</u> (%2, %3)%4%5</li>\n").arg(mountPtr->partition).arg(mountPtr->size).arg(mountPtr->currentfs).arg(newdata).arg(formatdata);
		}
		else notFormatted += QString("<li><u>%1</u> (%2, %3)%4%5</li>\n").arg(mountPtr->partition).arg(mountPtr->size).arg(mountPtr->currentfs).arg(newdata).arg(formatdata);
	}
	ui->confirmationTextBrowser->setText(tr("<p><b>Package source:</b> %1</p>\
				<p><b>Installation type:</b> %2</p>\
				<p><b>Partitions that will be FORMATTED:</b><ul>%3</ul></p>\
				<p><b>Partitions that will NOT be formatted but used:</b><ul>%4</ul></p>\
				<p><b>Boot loader will be installed to:</b> %5</p>").\
			arg(settings->value("pkgsource").toString()).\
			arg(settings->value("setup_variant").toString()).\
			arg(formatted).arg(notFormatted).\
			arg(settings->value("bootloader").toString()));
}

void MainWindow::saveRootPassword() {
	settings->setValue("rootpasswd", ui->rootPasswordEdit->text());
}

void MainWindow::saveUsers() {
	settings->beginGroup("users");
	settings->setValue(ui->usernameEdit->text(), ui->userPasswordEdit->text());
	settings->endGroup();
}

bool MainWindow::validateRootPassword() {
	if (ui->rootPasswordEdit->text()!=ui->rootPasswordVerifyEdit->text()) {
		QMessageBox::information(this, tr("Verification failed"), tr("Passwords doesn't match. Please enter it more carefully."));
		ui->rootPasswordVerifyEdit->clear();
		ui->rootPasswordEdit->clear();
		return false;
	}
	if (ui->rootPasswordEdit->text().isEmpty()) {
		QMessageBox::information(this, tr("Empty password"), tr("Root password cannot be empty, it is very insecure!"));
		return false;
	}
	return true;

}

bool MainWindow::validateUserPasswords() {
	if (ui->usernameEdit->text().isEmpty()) {
		QMessageBox::information(this, tr("Username not specified"), tr("Please, enter username"));
		return false;
	}
	if (ui->usernameEdit->text().toStdString().find_first_of("0123456789")==0) {
		QMessageBox::information(this, tr("Invalid username"), tr("Username cannot start from number. Please specify correct username."));
		return false;
	}
	if (ui->usernameEdit->text()=="root") {
		QMessageBox::information(this, tr("Invalid username"), tr("User root is an administrative account and already exists, please specify another name"));
		return false;
	}
	if (ui->usernameEdit->text().toStdString().find_first_not_of("abcdefghijklmnopqrstuvwxyz1234567890_-")==0) {
		QMessageBox::information(this, tr("Invalid username"), tr("Username contain invalid characters. Please specify correct username."));
		return false;
	}
	if (ui->userPasswordEdit->text()!=ui->userPasswordVerifyEdit->text()) {
		QMessageBox::information(this, tr("Verification failed"), tr("Passwords doesn't match. Please enter it more carefully."));
		ui->userPasswordVerifyEdit->clear();
		ui->userPasswordEdit->clear();
		return false;
	}
	if (ui->userPasswordEdit->text().isEmpty()) {
		QMessageBox::information(this, tr("Empty password"), tr("You cannot create user with no password. If you still want this, you can change password after installation."));
		return false;
	}
	return true;
}



void MainWindow::timezoneSearch(const QString &search) {
	for (int i=0; i<ui->timezoneListWidget->count(); ++i) {
		if (search.isEmpty()) {
			ui->timezoneListWidget->item(i)->setHidden(false);
			continue;
		}
		if (ui->timezoneListWidget->item(i)->text().contains(search, Qt::CaseInsensitive)) ui->timezoneListWidget->item(i)->setHidden(false);
		else ui->timezoneListWidget->item(i)->setHidden(true);
	}
}

bool MainWindow::validateMountPoints() {
	bool hasRoot = false, hasSwap = false;
	uint64_t rootSize = 0;
	bool hasSeparateBoot = false;
	QString bootfs;
	// Check for incorrect input: invalid mount points
	for (size_t i=0; i<mountOptions.size(); ++i) {
		if (mountOptions[i].mountpoint=="swap") continue;
		if (mountOptions[i].mountpoint.isEmpty()) continue;
		if (mountOptions[i].mountpoint[0]!='/') {
			QMessageBox::warning(this, tr("Invalid mount point"), tr("Mount point '%1' is invalid: it should be an absolute path. For more information, see help.").arg(mountOptions[i].mountpoint));
			return false;
		}
		if (mountOptions[i].mountpoint.toStdString().find_first_of(" \t\n\\\'\"()&$*?;><|")!=std::string::npos) {
			QMessageBox::warning(this, tr("Invalid mount point"), tr("Mount point '%1' contains invalid characters. For more information, see help.").arg(mountOptions[i].mountpoint));
			return false;
		}
		// Also we can check if we have separate /boot and check filesystem on it.
		if (mountOptions[i].mountpoint=="/boot") {
			hasSeparateBoot=true;
			if (mountOptions[i].newfs.isEmpty()) bootfs = mountOptions[i].currentfs;
			else bootfs = mountOptions[i].newfs;
		}
		// Check for filesystem if no formatting is performed
		if (!mountOptions[i].newfs.isEmpty()) continue;
		if (mountOptions[i].currentfs.isEmpty() || mountOptions[i].currentfs=="unformatted") {
			QMessageBox::warning(this, tr("Unformatted filesystem mount"), tr("You are attempting to mount an unformatted partition to '%1'. It is impossible, please mark it to format or leave unused.").arg(mountOptions[i].mountpoint));
			return false;

		}	
	}
	// Check for nessecary partitions
	for (size_t i=0; i<mountOptions.size(); ++i) {
		if (mountOptions[i].mountpoint == "swap") hasSwap = true;
		if (mountOptions[i].mountpoint == "/") {
			hasRoot = true;
			rootSize = mountOptions[i].psize;
			// If no separate boot - check for root filesystem too
			if (!hasSeparateBoot) {
				if (mountOptions[i].newfs.isEmpty()) bootfs = mountOptions[i].currentfs;
				else bootfs = mountOptions[i].newfs;
			}
		}
	}
	if (!hasSwap && QMessageBox::question(this, tr("No swap partition"), tr("You didn't specified swap partition. It is OK for systems with lots of RAM (2Gb or more), but you will be unable to use suspend-to-disk. Are you sure?"), QMessageBox::Yes|QMessageBox::No)!=QMessageBox::Yes) return false;
	if (!hasRoot) {
		QMessageBox::warning(this, tr("No root partition"), tr("You didn't specified root partition. Without this, system cannot be installed."));
		return false;
	}
	// Check boot filesystem
	if (bootfs!="ext2" && bootfs!="ext3" && bootfs!="ext4" && bootfs!="jfs" && bootfs!="reiserfs") {
		if (QMessageBox::question(this, tr("Unsupported root filesystem"), tr("Unfortunately, GRUB boot loader cannot be installed on %1 filesystem.\nYou can ignore this warning if you really know what are you going to do. \nDo you want to make your system bootable without red-eye horror?").arg(bootfs), QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)==QMessageBox::Yes) return false;
	}

	// Check for dupes
	bool dupesFound = false;
	for (size_t i=0; i<mountOptions.size(); ++i) {
		for (size_t t=0; t<mountOptions.size(); ++t) {
			if (i==t) continue;
			if (mountOptions[i].mountpoint.isEmpty()) continue;
			if (mountOptions[i].mountpoint == mountOptions[t].mountpoint) {
				QMessageBox::warning(this, tr("Duplicate mount points found"), tr("Duplicate mount points found: %1").arg(mountOptions[i].mountpoint));
				dupesFound = true;
			}
		}
	}
	if (dupesFound) return false;

	// Now check for enough free space
	string setupName = settings->value("setup_variant").toString().toStdString();
	for (size_t i=0; i<customPkgSetList.size(); ++i) {
		if (customPkgSetList[i].name==setupName) {
			if (customPkgSetList[i].isize + customPkgSetList[i].isize*0.2 > rootSize) {
				if (QMessageBox::question(this, tr("Not enough space"), tr("Size of root filesystem may be not enough for this installation type. You need to have at least %1 of space. Note that this check doesn't respect complex partitioning schemes such as separate /usr, so you can ignore this warning if you sure.\n\nDo you want to re-check your partitioning scheme?").arg(humanizeSize(customPkgSetList[i].isize).c_str()), QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)==QMessageBox::Yes) return false;
			}
		}
	}

	return true;
}

bool MainWindow::validateNetworking() {
	if (ui->hostnameEdit->text().isEmpty()) {
		QMessageBox::information(this, tr("Network settings error"), tr("Please specify hostname"));
		return false;
	}

	if (ui->netnameEdit->text().isEmpty()) {
		QMessageBox::information(this, tr("Network settings error"), tr("Please specify network name"));
		return false;
	}
	return true;
}

void MainWindow::saveNetworking() {
	settings->setValue("hostname", ui->hostnameEdit->text());
	settings->setValue("netname", ui->netnameEdit->text());
	if (ui->netNMRadioButton->isChecked()) settings->setValue("netman", "networkmanager");
	else if (ui->netWicdRadioButton->isChecked()) settings->setValue("netman", "wicd");
	else if (ui->netNetconfigRadioButton->isChecked()) settings->setValue("netman", "netconfig");
}

bool MainWindow::checkLoad(int page) {
	switch(page) {
		case PAGE_NVIDIA:
			return checkNvidiaLoad();
			break;
		default: 
			return true;
	}
}

bool MainWindow::checkNvidiaLoad() {
	if (hasNvidia==-1) {
		string tmp_hw = get_tmp_file();
		system("lspci > " + tmp_hw);
		vector<string> hw = ReadFileStrings(tmp_hw);
		for(size_t i=0; i<hw.size(); ++i) {
			if (hw[i].find("VGA compatible controller")!=std::string::npos && hw[i].find("nVidia")!=std::string::npos) hasNvidia = i;
		}
		if (hasNvidia == -1) hasNvidia = 0;
	}
	// For debugging reasons, check for /tmp/nvidia_force file
	if (FileExists("/tmp/nvidia_force")) hasNvidia = 1;
	if (!hasNvidia) {
		return false;
	}
	else {
		return true;
	}
}

void MainWindow::saveNvidia() {
	if (ui->nvidiaLatestRadioButton->isChecked()) settings->setValue("nvidia-driver", "latest");
	else if (ui->nvidia173RadioButton->isChecked()) settings->setValue("nvidia-driver", "173");
	else if (ui->nvidia96RadioButton->isChecked()) settings->setValue("nvidia-driver", "96");
	else if (ui->nvidiaNVRadioButton->isChecked()) settings->setValue("nvidia-driver", "nouveau");
}

void MainWindow::saveAlternatives() {
	QString alternatives;
	settings->remove("alternatives");
	//if (ui->altBFSCheckBox->isChecked()) settings->setValue("alternatives/bfs", true);
	if (ui->altCleartypeCheckBox->isChecked()) settings->setValue("alternatives/cleartype", true);
}

void MainWindow::showHideReleaseNotes() {
	if (ui->releaseNotesTextBrowser->isVisible()) {
		ui->releaseNotesButton->setText(tr("Show release notes"));
		ui->releaseNotesTextBrowser->hide();
	}
	else {
		ui->releaseNotesButton->setText(tr("Hide release notes"));
		ui->releaseNotesTextBrowser->show();
	}

}

void MainWindow::mountFilterNoFormat(bool enabled) {
	ui->mountFormatOptionsComboBox->setEnabled(!enabled);	
}
void MainWindow::mountFilterCustom(bool enabled) {
	ui->mountPointEdit->setEnabled(enabled);
	if (!enabled) return;
	ui->mountNoFormatRadioButton->setEnabled(true);
	ui->mountDoFormatRadioButton->setEnabled(true);
	
}

void MainWindow::mountFilterSwap(bool enabled) {
	if (!enabled) return;
	ui->mountNoFormatRadioButton->setEnabled(false);
	ui->mountDoFormatRadioButton->setEnabled(false);
	ui->mountNoFormatRadioButton->setChecked(true);
}
void MainWindow::mountFilterRoot(bool enabled) {
	if (!enabled) return;
	ui->mountNoFormatRadioButton->setEnabled(false);
	ui->mountDoFormatRadioButton->setEnabled(true);
	ui->mountDoFormatRadioButton->setChecked(true);
}

void MainWindow::mountFilterDontUse(bool enabled) {
	if (!enabled) return;
	ui->mountNoFormatRadioButton->setEnabled(false);
	ui->mountDoFormatRadioButton->setEnabled(false);
	ui->mountNoFormatRadioButton->setChecked(true);
}


