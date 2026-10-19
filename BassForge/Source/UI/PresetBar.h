#pragma once
#include "KnobKit.h"
#include "Presets/PresetManager.h"

namespace bf::ui
{
    /** Barra preset: [<] [elenco factory + utente] [>] [Save] [Init]. Il nome corrente vive nello stato (property "presetName"). */
    class PresetBar : public juce::Component
    {
    public:
        explicit PresetBar(PresetManager& m) : manager(m)
        {
            for (auto* b : { &prev, &next, &saveBtn, &initBtn }) addAndMakeVisible(*b);
            addAndMakeVisible(box);
            box.setTextWhenNothingSelected("Preset");
            box.onChange = [this] { onSelect(); };
            prev.onClick = [this] { step(-1); };
            next.onClick = [this] { step(+1); };
            saveBtn.onClick = [this] { askSave(); };
            initBtn.onClick = [this] { manager.loadFactory(0); refresh(); };
            refresh();
        }

        void refresh()
        {
            userFiles = manager.userPresets();
            box.clear(juce::dontSendNotification);
            box.addSectionHeading("Factory");
            for (int i = 0; i < PresetManager::numFactory(); ++i) box.addItem(PresetManager::factoryName(i), kFactoryBase + i);
            if (! userFiles.isEmpty())
            {
                box.addSeparator();
                box.addSectionHeading("User");
                for (int i = 0; i < userFiles.size(); ++i) box.addItem(userFiles[i].getFileNameWithoutExtension(), kUserBase + i);
            }
            showCurrent();
        }

        void resized() override
        {
            auto b = getLocalBounds();
            prev.setBounds(b.removeFromLeft(28));
            box.setBounds(b.removeFromLeft(240).reduced(2, 3));
            next.setBounds(b.removeFromLeft(28));
            b.removeFromLeft(10);
            saveBtn.setBounds(b.removeFromLeft(70).reduced(2, 3));
            initBtn.setBounds(b.removeFromLeft(70).reduced(2, 3));
        }

    private:
        static constexpr int kFactoryBase = 1, kUserBase = 1000;

        void showCurrent()
        {
            const auto n = manager.currentName();
            for (int i = 0; i < box.getNumItems(); ++i)
                if (box.getItemText(i) == n) { box.setSelectedItemIndex(i, juce::dontSendNotification); return; }
            box.setText(n, juce::dontSendNotification);
        }

        void onSelect()
        {
            const int id = box.getSelectedId();
            if (id >= kUserBase) { if (id - kUserBase < userFiles.size()) manager.load(userFiles[id - kUserBase]); }
            else if (id >= kFactoryBase) manager.loadFactory(id - kFactoryBase);
        }

        void step(int dir)
        {
            const int n = box.getNumItems();
            if (n == 0) return;
            int i = box.getSelectedItemIndex();
            for (int k = 0; k < n; ++k)                       // salta intestazioni/separatori
            {
                i = (i + dir + n) % n;
                if (box.getItemId(i) != 0) { box.setSelectedItemIndex(i, juce::sendNotificationSync); return; }
            }
        }

        void askSave()
        {
            auto* w = new juce::AlertWindow("Save preset", "Name:", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor("name", manager.currentName() == "Init" ? juce::String() : manager.currentName());
            w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            juce::Component::SafePointer<PresetBar> self(this);
            w->enterModalState(true, juce::ModalCallbackFunction::create([self, w](int r)
            {
                if (r == 1 && self != nullptr)
                {
                    self->manager.save(w->getTextEditorContents("name"));
                    self->refresh();
                }
            }), true);
        }

        PresetManager& manager;
        juce::ComboBox box;
        juce::TextButton prev { "<" }, next { ">" }, saveBtn { "Save" }, initBtn { "Init" };
        juce::Array<juce::File> userFiles;
    };
}
