/*
 // Copyright (c) 2021-2022 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

class CanvasObject final : public ObjectBase {

    bool locked;
    Value sizeProperty = SynchronousValue();

    IEMHelper iemHelper;

public:
    CanvasObject(void* ptr, Object* object)
        : ObjectBase(ptr, object)
        , iemHelper(ptr, object, this)
    {
        object->setColour(PlugDataColour::outlineColourId, Colours::transparentBlack);
        locked = getValue<bool>(object->locked);

        objectParameters.addParamSize(&sizeProperty);
        objectParameters.addParamColour("Canvas color", cGeneral, &iemHelper.secondaryColour, PlugDataColour::guiObjectInternalOutlineColour);
        iemHelper.addIemParameters(objectParameters, false, true, 20, 12, 14);
    }

    void updateSizeProperty() override
    {
        setPdBounds(object->getObjectBounds());

        if (auto canvasObj = ptr.get<t_my_canvas>()) {
            setParameterExcludingListener(sizeProperty, Array<var> { var(canvasObj->x_vis_w), var(canvasObj->x_vis_h) });
        }
    }

    bool hideInlets() override
    {
        return iemHelper.hasReceiveSymbol();
    }

    bool hideOutlets() override
    {
        return iemHelper.hasSendSymbol();
    }

    void updateLabel() override
    {
        iemHelper.updateLabel(label);
    }

    std::vector<hash32> getAllMessages() override
    {
        return {
            IEMGUI_MESSAGES
        };
    }

    void receiveObjectMessage(String const& symbol, std::vector<pd::Atom>& atoms) override
    {
        iemHelper.receiveObjectMessage(symbol, atoms);
    }

    void update() override
    {
        if (auto cnvObj = ptr.get<t_my_canvas>()) {
            sizeProperty = Array<var> { var(cnvObj->x_vis_w), var(cnvObj->x_vis_h) };
        }

        iemHelper.update();
    }

    Rectangle<int> getSelectableBounds() override
    {
        if (auto cnvObj = ptr.get<t_my_canvas>()) {
            return { cnvObj->x_gui.x_obj.te_xpix, cnvObj->x_gui.x_obj.te_ypix, cnvObj->x_gui.x_w, cnvObj->x_gui.x_h };
        }

        return {};
    }

    bool canReceiveMouseEvent(int x, int y) override
    {
        if (auto iemgui = ptr.get<t_iemgui>()) {
            return !locked && Rectangle<int>(iemgui->x_w, iemgui->x_h).contains(x - Object::margin, y - Object::margin);
        }

        return false;
    }

    void lock(bool isLocked) override
    {
        locked = isLocked;
    }

    void setPdBounds(Rectangle<int> b) override
    {
        if (auto cnvObj = ptr.get<t_my_canvas>()) {
            cnvObj->x_gui.x_obj.te_xpix = b.getX();
            cnvObj->x_gui.x_obj.te_ypix = b.getY();
            cnvObj->x_vis_w = b.getWidth() - 1;
            cnvObj->x_vis_h = b.getHeight() - 1;
        }
    }

    Rectangle<int> getPdBounds() override
    {
        if (auto canvas = ptr.get<t_my_canvas>()) {
            auto* patch = cnv->patch.getPointer().get();
            if (!patch)
                return {};

            int x = 0, y = 0, w = 0, h = 0;
            libpd_get_object_bounds(patch, canvas.get(), &x, &y, &w, &h);

            return Rectangle<int>(x, y, ptr.get<t_my_canvas>()->x_vis_w + 1, ptr.get<t_my_canvas>()->x_vis_h + 1);
        }

        return {};
    }

    void paint(Graphics& g) override
    {
        auto bgcolour = Colour::fromString(iemHelper.secondaryColour.toString());

        g.setColour(bgcolour);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), Corners::objectCornerRadius);

        if (!locked) {

            Rectangle<float> draggableRect;
            if (auto iemgui = ptr.get<t_iemgui>()) {
                draggableRect = Rectangle<float>(ptr.get<t_iemgui>()->x_w, ptr.get<t_iemgui>()->x_h);
            } else {
                return;
            }

            g.setColour(object->isSelected() ? object->findColour(PlugDataColour::objectSelectedOutlineColourId) : object->findColour(PlugDataColour::objectOutlineColourId));
            g.drawRoundedRectangle(draggableRect.reduced(1.0f), Corners::objectCornerRadius, 1.0f);
        }
    }

    void valueChanged(Value& v) override
    {
        if (v.refersToSameSourceAs(sizeProperty)) {
            auto& arr = *sizeProperty.getValue().getArray();
            auto* constrainer = getConstrainer();
            auto width = std::max(int(arr[0]), constrainer->getMinimumWidth());
            auto height = std::max(int(arr[1]), constrainer->getMinimumHeight());

            setParameterExcludingListener(sizeProperty, Array<var> { var(width), var(height) });

            if (auto cnvObj = ptr.get<t_my_canvas>()) {
                cnvObj->x_vis_w = width;
                cnvObj->x_vis_h = height;
            }

            object->updateBounds();
        } else {
            iemHelper.valueChanged(v);
        }
    }
};
