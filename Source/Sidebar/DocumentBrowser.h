/*
 // Copyright (c) 2021-2022 Timothy Schoen.
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

// This is based on juce_FileTreeComponent, but I decided to rewrite parts to:
// 1. Sort by folders first
// 2. Improve simplicity and efficiency by not using OS file icons (they look bad anyway)

#include "Utility/OSUtils.h"
#include "Object.h"

class DocumentBrowserSettings : public Component {
public:
    
    struct DocumentBrowserSettingsButton : public TextButton
    {
        const String icon;
        const String description;
        
        DocumentBrowserSettingsButton(String iconString, String descriptionString) : icon(iconString), description(descriptionString)
        {
        }
        
        void paint(Graphics& g) override
        {
            auto colour = findColour(PlugDataColour::toolbarTextColourId);

            Fonts::drawText(g, description, getLocalBounds().withTrimmedLeft(30), colour, 14);
            
            if(isMouseOver())
            {
                colour = colour.brighter(0.4f);
            }
            if(getToggleState())
            {
                colour = findColour(PlugDataColour::toolbarActiveColourId);
            }
     
            Fonts::drawIcon(g, icon, getLocalBounds().withTrimmedLeft(6), colour, 14, false);
        }
    };

    DocumentBrowserSettings(std::function<void()> chooseCustomLocation, std::function<void()> resetDefaultLocation)
    {
        addAndMakeVisible(customLocationButton);
        addAndMakeVisible(restoreLocationButton);
        
        customLocationButton.onClick = [chooseCustomLocation](){
            chooseCustomLocation();
        };
        
        restoreLocationButton.onClick = [resetDefaultLocation](){
            resetDefaultLocation();
        };

        setSize(180, 70);
    }

    void resized() override
    {
        auto buttonBounds = getLocalBounds();
        
        int buttonHeight = buttonBounds.getHeight() / 2;

        customLocationButton.setBounds(buttonBounds.removeFromTop(buttonHeight));
        restoreLocationButton.setBounds(buttonBounds.removeFromTop(buttonHeight));
    }

private:
    
    DocumentBrowserSettingsButton customLocationButton = DocumentBrowserSettingsButton(Icons::Open, "Show custom folder...");
    DocumentBrowserSettingsButton restoreLocationButton = DocumentBrowserSettingsButton(Icons::Restore, "Show default folder");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DocumentBrowserSettings)
};

// Base classes for communication between parent and child classes
class DocumentBrowserViewBase : public TreeView
    , public DirectoryContentsDisplayComponent {

public:
    DocumentBrowserViewBase(DirectoryContentsList& listToShow)
        : DirectoryContentsDisplayComponent(listToShow) {};
};

class DocumentBrowserBase : public Component {

public:
    DocumentBrowserBase(PluginProcessor* processor)
        : pd(processor)
        , filter("*", "*", "All files")
        , updateThread("browserThread")
        , directory(&filter, updateThread) {

        };

    virtual bool isSearching() = 0;

    PluginProcessor* pd;
    TimeSliceThread updateThread;
    DirectoryContentsList directory;
    WildcardFileFilter filter;
};

class DocumentBrowserItem : public TreeViewItem
    , private AsyncUpdater
    , private ChangeListener {
public:
    DocumentBrowserItem(DocumentBrowserViewBase& treeComp, DirectoryContentsList* parentContents, int indexInContents, int indexInParent, File const& f)
        : file(f)
        , owner(treeComp)
        , parentContentsList(parentContents)
        , indexInContentsList(indexInContents)
        , subContentsList(nullptr, false)
    {
        DirectoryContentsList::FileInfo fileInfo;

        if (parentContents != nullptr && parentContents->getFileInfo(indexInContents, fileInfo)) {
            fileSize = File::descriptionOfSizeInBytes(fileInfo.fileSize);
            isDirectory = fileInfo.isDirectory;
        } else {
            isDirectory = true;
        }
    }

    ~DocumentBrowserItem() override
    {
        clearSubItems();
        removeSubContentsList();
    }

    void paintOpenCloseButton(Graphics& g, Rectangle<float> const& area, Colour backgroundColour, bool isMouseOver) override
    {
        Path p;
        p.startNewSubPath(0.0f, 0.0f);
        p.lineTo(0.5f, 0.5f);
        p.lineTo(isOpen() ? 1.0f : 0.0f, isOpen() ? 0.0f : 1.0f);

        auto arrowArea = area.reduced(5, 9).translated(4, 0).toFloat();
        
        if(!isOpen())
        {
            arrowArea = arrowArea.reduced(1);
        }

        g.setColour(isSelected() ? getOwnerView()->findColour(PlugDataColour::sidebarActiveTextColourId) : getOwnerView()->findColour(PlugDataColour::sidebarTextColourId).withAlpha(isMouseOver ? 0.7f : 1.0f));
        g.strokePath(p, PathStrokeType(2.0f, PathStrokeType::curved, PathStrokeType::rounded), p.getTransformToScaleToFit(arrowArea, true));
    }

    bool mightContainSubItems() override
    {
        return isDirectory;
    }
    String getUniqueName() const override
    {
        return file.getFullPathName();
    }
    int getItemHeight() const override
    {
        return 26;
    }
    var getDragSourceDescription() override
    {
        return String();
    }

    void itemOpennessChanged(bool isNowOpen) override
    {
        if (isNowOpen) {
            clearSubItems();

            isDirectory = file.isDirectory();

            if (isDirectory) {
                if (subContentsList == nullptr && parentContentsList != nullptr) {
                    auto l = new DirectoryContentsList(parentContentsList->getFilter(), parentContentsList->getTimeSliceThread());

                    l->setDirectory(file, parentContentsList->isFindingDirectories(), parentContentsList->isFindingFiles());

                    setSubContentsList(l, true);
                }

                changeListenerCallback(nullptr);
            }
        }

        setSelected(true, true, sendNotificationSync);
        owner.repaint();
    }

    void removeSubContentsList()
    {
        if (subContentsList != nullptr) {
            subContentsList->removeChangeListener(this);
            subContentsList.reset();
        }
    }

    void setSubContentsList(DirectoryContentsList* newList, bool const canDeleteList)
    {
        removeSubContentsList();

        subContentsList = OptionalScopedPointer<DirectoryContentsList>(newList, canDeleteList);
        newList->addChangeListener(this);
    }

    void changeListenerCallback(ChangeBroadcaster*) override
    {
        rebuildItemsFromContentList();
    }

    void rebuildItemsFromContentList()
    {
        clearSubItems();

        if (isOpen() && subContentsList != nullptr) {
            int idx = 0;
            // Sort by folders first
            for (int i = 0; i < subContentsList->getNumFiles(); ++i) {
                if (subContentsList->getFile(i).isDirectory()) {
                    addSubItem(new DocumentBrowserItem(owner, subContentsList, i, idx, subContentsList->getFile(i)));
                    idx++;
                }
            }
            for (int i = 0; i < subContentsList->getNumFiles(); ++i) {
                if (subContentsList->getFile(i).existsAsFile()) {
                    addSubItem(new DocumentBrowserItem(owner, subContentsList, i, idx, subContentsList->getFile(i)));
                    idx++;
                }
            }
        }
    }

    void paintItem(Graphics& g, int width, int height) override
    {
        int const x = 24;

        auto colour = isSelected() ? owner.findColour(PlugDataColour::sidebarActiveTextColourId) : owner.findColour(PlugDataColour::sidebarTextColourId);

        if (isDirectory) {
            Fonts::drawIcon(g, Icons::Folder, Rectangle<int>(6, 2, x - 4, height - 4), colour, 12, false);
        } else {
            Fonts::drawIcon(g, Icons::File, Rectangle<int>(6, 2, x - 4, height - 4), colour, 12, false);
        }

        Fonts::drawFittedText(g, file.getFileName(), x, 0, width - x, height, colour);
    }

    String getAccessibilityName() override
    {
        return file.getFileName();
    }

    bool selectFile(File const& target)
    {
        if (file == target) {
            setSelected(true, true);
            return true;
        }

        if (target.isAChildOf(file)) {
            setOpen(true);

            for (int maxRetries = 500; --maxRetries > 0;) {
                for (int i = 0; i < getNumSubItems(); ++i)
                    if (auto* f = dynamic_cast<DocumentBrowserItem*>(getSubItem(i)))
                        if (f->selectFile(target))
                            return true;

                // if we've just opened and the contents are still loading, wait for it..
                if (subContentsList != nullptr && subContentsList->isStillLoading()) {
                    Thread::sleep(10);
                    rebuildItemsFromContentList();
                } else {
                    break;
                }
            }
        }

        return false;
    }

    void itemClicked(MouseEvent const& e) override
    {
        owner.sendMouseClickMessage(file, e);
    }

    void itemDoubleClicked(MouseEvent const& e) override
    {
        TreeViewItem::itemDoubleClicked(e);

        owner.sendDoubleClickMessage(file);
    }

    void itemSelectionChanged(bool) override
    {
        owner.sendSelectionChangeMessage();
    }

    void handleAsyncUpdate() override
    {
        owner.repaint();
    }

    const File file;

private:
    DocumentBrowserViewBase& owner;
    DirectoryContentsList* parentContentsList;
    int indexInContentsList;
    OptionalScopedPointer<DirectoryContentsList> subContentsList;
    bool isDirectory;
    String fileSize;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DocumentBrowserItem)
};

class DocumentBrowserView : public DocumentBrowserViewBase
    , public FileBrowserListener
    , public ScrollBar::Listener
    , public Timer {
public:
    /** Creates a listbox to show the contents of a specified directory.
     */
    DocumentBrowserView(DirectoryContentsList& listToShow, DocumentBrowserBase* parent)
        : DocumentBrowserViewBase(listToShow)
        , itemHeight(24)
        , browser(parent)
        , lastUpdateTime(listToShow.getDirectory().getLastModificationTime())
    {
        setIndentSize(16);
        setRootItemVisible(false);
        refresh();
        addListener(this);
        getViewport()->getVerticalScrollBar().addListener(this);
        getViewport()->setScrollBarsShown(true, false);
        startTimer(1500);
    }

    void timerCallback() override
    {
        auto lastModificationTime = directoryContentsList.getDirectory().getLastModificationTime();
        if (lastModificationTime > lastUpdateTime) {
            refresh();
            lastUpdateTime = lastModificationTime;
        }
    }

    /** Destructor. */
    ~DocumentBrowserView() override
    {
        deleteRootItem();
    }

    /** Scrolls this view to the top. */
    void scrollToTop() override
    {
        getViewport()->getVerticalScrollBar().setCurrentRangeStart(0);
    }
    /** If the specified file is in the list, it will become the only selected item
        (and if the file isn't in the list, all other items will be deselected). */
    void setSelectedFile(File const& target) override
    {
        if (auto* t = dynamic_cast<DocumentBrowserItem*>(getRootItem()))
            if (!t->selectFile(target))
                clearSelectedItems();
    }

    /** Returns the number of files the user has got selected.
        @see getSelectedFile
    */
    int getNumSelectedFiles() const override
    {
        return TreeView::getNumSelectedItems();
    }

    /** Returns one of the files that the user has currently selected.
        The index should be in the range 0 to (getNumSelectedFiles() - 1).
        @see getNumSelectedFiles
    */
    File getSelectedFile(int index = 0) const override
    {
        if (auto* item = dynamic_cast<DocumentBrowserItem const*>(getSelectedItem(index)))
            return item->file;

        return {};
    }

    /** Deselects any files that are currently selected. */
    void deselectAllFiles() override
    {
        clearSelectedItems();
    }

    /** Updates the files in the list. */
    void refresh()
    {
        directoryContentsList.refresh();

        // Mouse events during update can cause a crash!
        setEnabled(false);

        // Prevents crash!
        setRootItemVisible(false);

        deleteRootItem();

        auto root = new DocumentBrowserItem(*this, nullptr, 0, 0, directoryContentsList.getDirectory());

        root->setSubContentsList(&directoryContentsList, false);
        setRootItem(root);

        setInterceptsMouseClicks(true, true);
        setEnabled(true);
    }

    void paint(Graphics& g) override
    {
        // Paint selected row
        if (getNumSelectedFiles()) {
            g.setColour(findColour(PlugDataColour::sidebarActiveBackgroundColourId));

            auto y = getSelectedItem(0)->getItemPosition(true).getY();
            auto selectedRect = Rectangle<float>(3.0f, y + 2.0f, getWidth() - 6.0f, 22.0f);

            g.fillRoundedRectangle(selectedRect, Corners::defaultCornerRadius);
        }
    }
    // Paint file drop outline
    void paintOverChildren(Graphics& g) override
    {
        if (isDraggingFile) {
            g.setColour(findColour(PlugDataColour::scrollbarThumbColourId));
            g.drawRect(getLocalBounds().reduced(1), 2.0f);
        }
    }

    void scrollBarMoved(ScrollBar* scrollBarThatHasMoved, double newRangeStart) override
    {
        repaint();
    }

    /** Callback when the user double-clicks on a file in the browser. */
    void fileDoubleClicked(File const& file) override
    {
        if (file.isDirectory()) {
            file.revealToUser();
        } else if (file.existsAsFile() && file.hasFileExtension("pd")) {
            browser->pd->loadPatch(file);
            SettingsFile::getInstance()->addToRecentlyOpened(file);
        } else if (file.existsAsFile()) {
            auto* editor = dynamic_cast<PluginEditor*>(browser->pd->getActiveEditor());
            if (auto* cnv = editor->getCurrentCanvas()) {
                cnv->attachNextObjectToMouse = true;

                auto lastPosition = cnv->viewport->getViewArea().getConstrainedPoint(cnv->getMouseXYRelative() - Point<int>(Object::margin, Object::margin));
                auto filePath = file.getFullPathName().replaceCharacter('\\', '/');
                cnv->objects.add(new Object(cnv, "msg " + filePath, lastPosition));
            }
        }
    }

    void selectionChanged() override
    {
        browser->repaint();
    };

    void fileClicked(File const&, MouseEvent const&) override {};
    void browserRootChanged(File const&) override {};

    bool isInterestedInFileDrag(StringArray const& files) override
    {
        if (!browser->isVisible())
            return false;

        if (browser->isSearching()) {
            return false;
        }

        for (auto& path : files) {
            auto file = File(path);
            if (file.exists() && (file.isDirectory() || file.hasFileExtension("pd"))) {
                return true;
            }
        }

        return false;
    }

    void filesDropped(StringArray const& files, int x, int y) override
    {
        for (auto& path : files) {
            auto file = File(path);

            if (file.exists() && (file.isDirectory() || file.hasFileExtension("pd"))) {
                auto alias = browser->directory.getDirectory().getChildFile(file.getFileName());

#if JUCE_WINDOWS
                if (alias.exists())
                    alias.deleteRecursively();

                // Symlinks on Windows are weird!
                if (file.isDirectory()) {

                    // Create NTFS directory junction
                    OSUtils::createJunction(alias.getFullPathName().replaceCharacters("/", "\\").toStdString(), file.getFullPathName().toStdString());
                } else {
                    // Create hard link
                    OSUtils::createHardLink(alias.getFullPathName().replaceCharacters("/", "\\").toStdString(), file.getFullPathName().toStdString());
                }

#else
                file.createSymbolicLink(alias, true);
#endif
            }
        }

        browser->directory.refresh();

        isDraggingFile = false;
        repaint();
    }

    void fileDragEnter(StringArray const&, int, int) override
    {
        isDraggingFile = true;
        repaint();
    }

    void fileDragExit(StringArray const&) override
    {
        isDraggingFile = false;
        repaint();
    }

private:
    DocumentBrowserBase* browser;
    bool isDraggingFile = false;

    Time lastUpdateTime;
    int itemHeight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DocumentBrowserView)
};

class FileSearchComponent : public Component
    , public ListBoxModel
    , public ScrollBar::Listener
    , public KeyListener {
public:
    FileSearchComponent(DirectoryContentsList& directory)
        : searchPath(directory)
    {
        listBox.setModel(this);
        listBox.setRowHeight(32);
        listBox.setOutlineThickness(0);
        listBox.deselectAllRows();

        listBox.getViewport()->setScrollBarsShown(true, false, false, false);

        input.getProperties().set("NoOutline", true);
        input.addKeyListener(this);
        input.onTextChange = [this]() {
            bool notEmpty = input.getText().isNotEmpty();
            if (listBox.isVisible() != notEmpty) {
                listBox.setVisible(notEmpty);
                getParentComponent()->repaint();
            }

            setInterceptsMouseClicks(notEmpty, true);
            updateResults(input.getText());
        };

        clearButton.getProperties().set("Style", "SmallIcon");
        clearButton.onClick = [this]() {
            input.clear();
            input.giveAwayKeyboardFocus();
            listBox.setVisible(false);
            setInterceptsMouseClicks(false, true);
            input.repaint();
        };

        input.setInterceptsMouseClicks(true, true);
        clearButton.setAlwaysOnTop(true);

        addAndMakeVisible(clearButton);
        addAndMakeVisible(listBox);
        addAndMakeVisible(input);

        listBox.addMouseListener(this, true);
        listBox.setVisible(false);

        input.setJustification(Justification::centredLeft);
        input.setBorder({ 1, 23, 3, 1 });

        listBox.setColour(ListBox::backgroundColourId, Colours::transparentBlack);

        listBox.getViewport()->getVerticalScrollBar().addListener(this);

        setInterceptsMouseClicks(false, true);

        lookAndFeelChanged();
        repaint();
    }

    void mouseDoubleClick(MouseEvent const& e) override
    {
        int row = listBox.getSelectedRow();

        if (isPositiveAndBelow(row, searchResult.size())) {
            if (listBox.getRowPosition(row, true).contains(e.getEventRelativeTo(&listBox).getPosition())) {
                openFile(searchResult.getReference(row));
            }
        }
    }

    // Divert up/down key events from text editor to the listbox
    bool keyPressed(KeyPress const& key, Component* originatingComponent) override
    {
        if (key.isKeyCode(KeyPress::upKey) || key.isKeyCode(KeyPress::downKey)) {
            listBox.keyPressed(key);
            return true;
        }

        return false;
    }

    void scrollBarMoved(ScrollBar* scrollBarThatHasMoved, double newRangeStart) override
    {
        repaint();
    }

    void paint(Graphics& g) override
    {
        if (listBox.isVisible()) {
            g.fillAll(findColour(PlugDataColour::sidebarBackgroundColourId));
        }
    }

    void lookAndFeelChanged() override
    {
        input.setColour(TextEditor::backgroundColourId, findColour(PlugDataColour::searchBarColourId));
        input.setColour(TextEditor::textColourId, findColour(PlugDataColour::sidebarTextColourId));
    }

    void paintOverChildren(Graphics& g) override
    {

        auto textColour = findColour(PlugDataColour::sidebarTextColourId);

        Fonts::drawIcon(g, Icons::Search, 0, 0, 30, textColour, 12);

        if (input.getText().isEmpty()) {
            Fonts::drawFittedText(g, "Type to search documentation", 30, 0, getWidth() - 60, 30, textColour.withAlpha(0.5f), 1, 0.9f, 14);
        }
    }

    void paintListBoxItem(int rowNumber, Graphics& g, int w, int h, bool rowIsSelected) override
    {
        if (rowIsSelected) {
            g.setColour(findColour(PlugDataColour::sidebarActiveBackgroundColourId));
            g.fillRoundedRectangle(5, 2, w - 10, h - 4, Corners::defaultCornerRadius);
        }

        auto colour = rowIsSelected ? findColour(PlugDataColour::sidebarActiveTextColourId) : findColour(ComboBox::textColourId);
        const String item = searchResult[rowNumber].getFileName();

        Fonts::drawText(g, item, h + 4, 0, w - 4, h, colour);
        Fonts::drawIcon(g, Icons::File, 12, 0, h, colour, 12, false);
    }

    int getNumRows() override
    {
        return searchResult.size();
    }

    Component* refreshComponentForRow(int rowNumber, bool isRowSelected, Component* existingComponentToUpdate) override
    {
        delete existingComponentToUpdate;
        return nullptr;
    }

    void clearSearchResults()
    {
        searchResult.clear();
    }

    void updateResults(String query)
    {
        clearSearchResults();

        if (query.isEmpty())
            return;

        auto addFile = [this, &query](File const& file) {
            auto fileName = file.getFileName();

            if (!file.hasFileExtension("pd"))
                return;

            if (fileName.containsIgnoreCase(query)) {
                searchResult.add(file);
            }
        };

        for (int i = 0; i < searchPath.getNumFiles(); i++) {
            auto file = searchPath.getFile(i);

            if (file.isDirectory()) {
                for (auto& child : OSUtils::iterateDirectory(file, true, false)) {
                    addFile(child);
                }
            } else {
                addFile(file);
            }
        }

        listBox.updateContent();
        listBox.repaint();

        if (listBox.getSelectedRow() == -1)
            listBox.selectRow(0, true, true);
    }

    bool hasSelection()
    {
        return listBox.isVisible() && isPositiveAndBelow(listBox.getSelectedRow(), searchResult.size());
    }
    bool isSearching()
    {
        return listBox.isVisible();
    }

    File getSelection()
    {
        int row = listBox.getSelectedRow();

        if (isPositiveAndBelow(row, searchResult.size())) {
            return searchResult[row];
        }

        return File();
    }

    void resized() override
    {
        auto tableBounds = getLocalBounds();
        auto inputBounds = tableBounds.removeFromTop(28);

        tableBounds.removeFromTop(4);

        input.setBounds(inputBounds);

        clearButton.setBounds(inputBounds.removeFromRight(32));

        listBox.setBounds(tableBounds);
    }

    std::function<void(File&)> openFile;

private:
    ListBox listBox;

    DirectoryContentsList& searchPath;
    Array<File> searchResult;
    TextEditor input;
    TextButton clearButton = TextButton(Icons::ClearText);
};

class DocumentBrowser : public DocumentBrowserBase {

public:
    DocumentBrowser(PluginProcessor* processor)
        : DocumentBrowserBase(processor)
        , fileList(directory, this)
        , searchComponent(directory)
    {
        auto location = File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory).getChildFile("plugdata").getChildFile("Library");

        if (SettingsFile::getInstance()->hasProperty("browser_path")) {
            auto customLocation = File(pd->settingsFile->getProperty<String>("browser_path"));
            if (customLocation.exists()) {
                location = customLocation;
            }
        }

        directory.setDirectory(location, true, true);

        updateThread.startThread();

        addAndMakeVisible(fileList);

        searchComponent.openFile = [this](File& file) {
            if (file.existsAsFile()) {
                pd->loadPatch(file);
                SettingsFile::getInstance()->addToRecentlyOpened(file);
            }
        };

        addAndMakeVisible(searchComponent);

        if (!fileList.getSelectedFile().exists())
            fileList.moveSelectedRow(1);
    }

    ~DocumentBrowser()
    {
        updateThread.stopThread(1000);
    }

    bool isSearching() override
    {
        return searchComponent.isSearching();
    }

    bool hitTest(int x, int y) override
    {
        if (x < 5)
            return false;

        return true;
    }

    void resized() override
    {
        searchComponent.setBounds(getLocalBounds().withHeight(getHeight() - 30));
        fileList.setBounds(getLocalBounds().withHeight(getHeight() - 60).withY(30).reduced(2, 0));
    }

    void paintOverChildren(Graphics& g) override
    {
        g.setColour(findColour(PlugDataColour::toolbarOutlineColourId));
        g.drawLine(0.5f, 0, 0.5f, getHeight() - 27.5f);

        g.setColour(findColour(PlugDataColour::toolbarOutlineColourId));
        g.drawLine(0, 29, getWidth(), 29);
    }
    
    void showCalloutBox(Rectangle<int> bounds, PluginEditor* editor)
    {
        auto openFolderCallback = [this]() {
            openChooser = std::make_unique<FileChooser>("Open...", directory.getDirectory().getFullPathName(), "", SettingsFile::getInstance()->wantsNativeDialog());

            openChooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                [this](FileChooser const& fileChooser) {
                    const auto file = fileChooser.getResult();
                    if (file.exists()) {
                        auto path = file.getFullPathName();
                        pd->settingsFile->setProperty("browser_path", path);
                        directory.setDirectory(path, true, true);
                    }
                });
        };
    
        
        auto resetFolderCallback = [this]() {
            auto location = File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory).getChildFile("plugdata").getChildFile("Library");
            auto path = location.getFullPathName();
            pd->settingsFile->setProperty("browser_path", path);
            directory.setDirectory(path, true, true);
        };

        
        auto docsSettings = std::make_unique<DocumentBrowserSettings>(openFolderCallback, resetFolderCallback);
        CallOutBox::launchAsynchronously(std::move(docsSettings), bounds, editor);
    }

private:
    TextButton revealButton = TextButton(Icons::OpenedFolder);
    TextButton loadFolderButton = TextButton(Icons::Folder);
    TextButton resetFolderButton = TextButton(Icons::Restore);

    std::unique_ptr<FileChooser> openChooser;

public:
    DocumentBrowserView fileList;
    FileSearchComponent searchComponent;
};
