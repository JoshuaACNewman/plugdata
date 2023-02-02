/*
 // Copyright (c) 2021-2022 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

class ToggleObject final : public ObjectBase {
    bool toggleState = false;
    bool alreadyToggled = false;
    Value nonZero;

    float value = 0.0f;

    IEMHelper iemHelper;

public:
    ToggleObject(void* ptr, Object* object)
        : ObjectBase(ptr, object)
        , iemHelper(ptr, object, this)
    {
        value = getValue();
        object->constrainer->setFixedAspectRatio(1);
    }

    void updateLabel() override
    {
        iemHelper.updateLabel(label);
    }

    void updateBounds() override
    {
        iemHelper.updateBounds();
    }

    void applyBounds() override
    {
        iemHelper.applyBounds();
    }

    void initialiseParameters() override
    {
        nonZero = static_cast<t_toggle*>(ptr)->x_nonzero;
        iemHelper.initialiseParameters();
        setToggleStateFromFloat(value);
    }

    void paint(Graphics& g) override
    {
        g.setColour(iemHelper.getBackgroundColour());
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), PlugDataLook::objectCornerRadius);

        bool selected = cnv->isSelected(object) && !cnv->isGraph;
        auto outlineColour = object->findColour(selected ? PlugDataColour::objectSelectedOutlineColourId : objectOutlineColourId);

        g.setColour(outlineColour);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), PlugDataLook::objectCornerRadius, 1.0f);

        auto toggledColour = iemHelper.getForegroundColour();
        auto untoggledColour = toggledColour.interpolatedWith(iemHelper.getBackgroundColour(), 0.8f);
        g.setColour(toggleState ? toggledColour : untoggledColour);

        auto crossBounds = getLocalBounds().reduced((getWidth() * 0.08f) + 4.5f).toFloat();

        if (getWidth() < 18) {
            crossBounds = getLocalBounds().toFloat().reduced(3.5f);
        }

        auto const max = std::max(crossBounds.getWidth(), crossBounds.getHeight());
        auto const strokeWidth = std::max(max * 0.15f, 2.0f);

        g.drawLine({ crossBounds.getTopLeft(), crossBounds.getBottomRight() }, strokeWidth);
        g.drawLine({ crossBounds.getBottomLeft(), crossBounds.getTopRight() }, strokeWidth);
    }

    void toggleObject(Point<int> position) override
    {
        if (!alreadyToggled) {
            startEdition();
            auto newValue = value != 0 ? 0 : static_cast<float>(nonZero.getValue());
            sendFloatValue(newValue);
            setToggleStateFromFloat(newValue);
            stopEdition();
            alreadyToggled = true;
        }
    }

    void untoggleObject() override
    {
        alreadyToggled = false;
        repaint();
    }

    void mouseDown(MouseEvent const& e) override
    {
        startEdition();
        auto newValue = value != 0 ? 0 : static_cast<float>(nonZero.getValue());
        sendFloatValue(newValue);
        setToggleStateFromFloat(newValue);
        stopEdition();

        // Make sure we don't re-toggle with an accidental drag
        alreadyToggled = true;
    }

    ObjectParameters getParameters() override
    {
        ObjectParameters allParameters = {
            { "Non-zero value", tFloat, cGeneral, &nonZero, {} }
        };

        auto iemParameters = iemHelper.getParameters();
        allParameters.insert(allParameters.end(), iemParameters.begin(), iemParameters.end());

        return allParameters;
    }

    void setToggleStateFromFloat(float value)
    {
        toggleState = std::abs(value) > std::numeric_limits<float>::epsilon();
        repaint();
    }

    void receiveObjectMessage(String const& symbol, std::vector<pd::Atom>& atoms) override
    {
        switch (objectMessageMapped[symbol]) {
        case objectMessage::msg_bang: {
            value = !value;
            setToggleStateFromFloat(value);
            break;
        }
        case objectMessage::msg_float:
        case objectMessage::msg_set: {
            value = atoms[0].getFloat();
            setToggleStateFromFloat(value);
            break;
        }
        case objectMessage::msg_nonzero: {
            if (atoms.size() >= 1)
                setParameterExcludingListener(nonZero, atoms[0].getFloat());
            break;
        }
        default: {
            iemHelper.receiveObjectMessage(symbol, atoms);
            break;
        }
        }
    }

    void valueChanged(Value& value) override
    {
        if (value.refersToSameSourceAs(nonZero)) {
            float val = nonZero.getValue();
            static_cast<t_toggle*>(ptr)->x_nonzero = val;
        } else {
            iemHelper.valueChanged(value);
        }
    }

    float getValue()
    {
        return (static_cast<t_toggle*>(ptr))->x_on;
    }
};
