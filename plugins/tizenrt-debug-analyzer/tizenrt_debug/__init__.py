"""Offline, evidence-first TizenRT diagnostic log analysis."""

from .analyzer import analyze, log_lines, render_markdown

__version__ = "0.1.0"
__all__ = ["analyze", "log_lines", "render_markdown"]
