# routes/node_api.py
"""API endpoints for managing connected HopFog fog nodes."""

from fastapi import APIRouter
from services.node_registry import node_registry
from services.xbee_service import xbee_service

router = APIRouter(prefix="/api/nodes", tags=["Node Management"])


@router.get("/")
def list_nodes():
    """List all registered fog nodes with their status."""
    nodes = node_registry.get_all()
    return {
        "total": len(nodes),
        "active": node_registry.count_active(),
        "nodes": nodes,
    }


@router.post("/{node_id}/sync")
def trigger_sync(node_id: str):
    """Manually trigger a data sync to a specific node."""
    from services.node_protocol import trigger_sync_request
    trigger_sync_request(node_id)
    return {"success": True, "message": f"Sync sent to {node_id}"}


@router.post("/{node_id}/get-stats")
def request_stats(node_id: str):
    """Ask a node to report its stats."""
    xbee_service.send_json({"cmd": "GET_STATS", "node_id": node_id})
    return {"success": True, "message": f"Stats request sent to {node_id}"}
