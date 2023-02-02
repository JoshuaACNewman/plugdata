/*
 // Copyright (c) 2021-2022 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

#pragma once

#include <JuceHeader.h>

extern "C" {
#include <m_pd.h>
}

#include <concurrentqueue.h>
#include "Iolet.h"
#include "Pd/PdInstance.h"

using PathPlan = std::vector<Point<float>>;

class Canvas;
class PathUpdater;

class Connection : public Component
    , public ComponentListener
    , public Value::Listener
    , public pd::MessageListener
    , public SettableTooltipClient {
public:
    int inIdx;
    int outIdx;

    SafePointer<Iolet> inlet, outlet;
    SafePointer<Object> inobj, outobj;

    Path toDraw;
    String lastId;

    Connection(Canvas* parent, Iolet* start, Iolet* end, void* oc);
    ~Connection() override;

    static void renderConnectionPath(Graphics& g, Canvas* cnv, Path connectionPath, bool isSignal, bool isMouseOver = false, bool isSelected = false, Point<int> mousePos = {0, 0});
        
    void paint(Graphics&) override;

    bool isSegmented();
    void setSegmented(bool segmented);

    void updatePath();

    bool hitTest(int x, int y) override;

    void mouseDown(MouseEvent const& e) override;
    void mouseMove(MouseEvent const& e) override;
    void mouseDrag(MouseEvent const& e) override;
    void mouseUp(MouseEvent const& e) override;
    void mouseExit(MouseEvent const& e) override;

    Point<float> getStartPoint();
    Point<float> getEndPoint();

    void reconnect(Iolet* target, bool dragged);

    bool intersects(Rectangle<float> toCheck, int accuracy = 4) const;
    int getClosestLineIdx(Point<float> const& position, PathPlan const& plan);

    String getId() const;

    void setPointer(void* ptr);
    void* getPointer();

    t_symbol* getPathState();
    void pushPathState();
    void popPathState();

    void componentMovedOrResized(Component& component, bool wasMoved, bool wasResized) override;

    // Pathfinding
    int findLatticePaths(PathPlan& bestPath, PathPlan& pathStack, Point<float> start, Point<float> end, Point<float> increment);

    void findPath();

    bool intersectsObject(Object* object);
    bool straightLineIntersectsObject(Line<float> toCheck, Array<Object*>& objects);

    void receiveMessage(String const& name, int argc, t_atom* argv) override;

private:
    bool wasSelected = false;
    bool segmented = false;

    Array<SafePointer<Connection>> reconnecting;

    Rectangle<float> startReconnectHandle, endReconnectHandle;

    PathPlan currentPlan;

    Value locked;
    Value presentationMode;

    Canvas* cnv;

    Point<float> origin, offset;

    int dragIdx = -1;

    float mouseDownPosition = 0;

    void valueChanged(Value& v) override;

    pd::t_fake_outconnect* ptr;

    friend class ConnectionPathUpdater;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Connection)
};

class ConnectionBeingCreated : public Component
{
    SafePointer<Iolet> iolet;
    Component* cnv;
    Path connectionPath;
public:
    ConnectionBeingCreated(Iolet* target, Component* canvas) : iolet(target), cnv(canvas){
        
        // Only listen for mouse-events on canvas and the original iolet
        setInterceptsMouseClicks(false, true);
        cnv->addMouseListener(this, true);
        iolet->addMouseListener(this, false);
        
        cnv->addAndMakeVisible(this);
    }
    
    ~ConnectionBeingCreated() {
        cnv->removeMouseListener(this);
        iolet->removeMouseListener(this);
    }
    
    void mouseDrag(const MouseEvent& e) override
    {
        mouseMove(e);
    }
    
    void mouseMove(const MouseEvent& e) override
    {
        auto ioletPoint = cnv->getLocalPoint((Component*)iolet->object, iolet->getBounds().getCentre());
        auto cursorPoint = cnv->getLocalPoint(nullptr, e.getScreenPosition());
                                              
        connectionPath.clear();
        connectionPath.startNewSubPath(ioletPoint.toFloat());
        connectionPath.lineTo(cursorPoint.toFloat());
        
        auto bounds = connectionPath.getBounds().getSmallestIntegerContainer().expanded(3);
        
        // Make sure we have minimal bounds, expand slightly to take line thickness into account
        setBounds(bounds);
        
        // Remove bounds offset from path, because we've already set our origin by setting component bounds
        connectionPath.applyTransform(AffineTransform::translation(-bounds.getX(), -bounds.getY()));
                                                    
        repaint();
    }
    
    void paint(Graphics& g) override
    {
        Connection::renderConnectionPath(g, (Canvas*)cnv, connectionPath, iolet->isSignal);
    }
    
    Iolet* getIolet() {
        return iolet;
    }
};

// Helper class to group connection path changes together into undoable/redoable actions
class ConnectionPathUpdater : public Timer {
    Canvas* canvas;

    moodycamel::ConcurrentQueue<std::pair<Component::SafePointer<Connection>, t_symbol*>> connectionUpdateQueue = moodycamel::ConcurrentQueue<std::pair<Component::SafePointer<Connection>, t_symbol*>>(4096);

    void timerCallback() override;

public:
    ConnectionPathUpdater(Canvas* cnv)
        : canvas(cnv) {};

    void pushPathState(Connection* connection, t_symbol* newPathState)
    {
        connectionUpdateQueue.enqueue({ connection, newPathState });
        startTimer(50);
    }
};
