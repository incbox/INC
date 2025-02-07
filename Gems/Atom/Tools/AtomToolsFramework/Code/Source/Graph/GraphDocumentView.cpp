/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AtomToolsFramework/Graph/GraphDocumentRequestBus.h>
#include <AtomToolsFramework/Graph/GraphDocumentView.h>

namespace AtomToolsFramework
{
    GraphDocumentView::GraphDocumentView(
        const AZ::Crc32& toolId, const AZ::Uuid& documentId, GraphViewSettingsPtr graphViewSettingsPtr, QWidget* parent)
        : GraphView(toolId, GraphCanvas::GraphId(), graphViewSettingsPtr, parent)
        , m_documentId(documentId)
    {
        AtomToolsDocumentNotificationBus::Handler::BusConnect(m_toolId);
        OnDocumentOpened(m_documentId);
        m_openedBefore = false;
    }

    GraphDocumentView::~GraphDocumentView()
    {
        AtomToolsDocumentNotificationBus::Handler::BusDisconnect();
    }

    void GraphDocumentView::OnDocumentOpened(const AZ::Uuid& documentId)
    {
        if (m_documentId == documentId)
        {
            GraphCanvas::GraphId activeGraphId = GraphCanvas::GraphId();
            GraphDocumentRequestBus::EventResult(activeGraphId, m_documentId, &GraphDocumentRequestBus::Events::GetGraphId);
            SetActiveGraphId(activeGraphId, true);

            // Show the entire graph and center the view the first time a graph is opened
            if (!m_openedBefore && activeGraphId.IsValid())
            {
                GraphCanvas::ViewId viewId;
                GraphCanvas::SceneRequestBus::EventResult(viewId, activeGraphId, &GraphCanvas::SceneRequests::GetViewId);
                GraphCanvas::ViewRequestBus::Event(viewId, &GraphCanvas::ViewRequests::ShowEntireGraph);
                m_openedBefore = true;
            }
            return;
        }

        SetActiveGraphId(GraphCanvas::GraphId(), false);
    }

    void GraphDocumentView::OnDocumentClosed([[maybe_unused]] const AZ::Uuid& documentId)
    {
        if (m_documentId == documentId)
        {
            SetActiveGraphId(GraphCanvas::GraphId(), true);
        }
    }

    void GraphDocumentView::OnDocumentDestroyed([[maybe_unused]] const AZ::Uuid& documentId)
    {
        if (m_documentId == documentId)
        {
            SetActiveGraphId(GraphCanvas::GraphId(), true);
        }
    }
} // namespace AtomToolsFramework
