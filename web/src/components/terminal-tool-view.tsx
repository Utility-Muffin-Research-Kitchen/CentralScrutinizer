"use client";

import { useEffect, useRef, useState } from "react";
import type { FitAddon } from "@xterm/addon-fit";
import type { Terminal } from "@xterm/xterm";
import "@xterm/xterm/css/xterm.css";

import { createTerminalSession } from "../lib/api";
import type { SessionResponse } from "../lib/types";
import { NoticeToast } from "./notice-toast";
import { useT } from "../lib/i18n";

function describeTerminalError(error: string, t: (key: string) => string): string {
  if (error === "terminal_disabled") {
    return t("Terminal access was disabled on the handheld.");
  }

  return t("Terminal connection failed.");
}

export function TerminalToolView({
  enabled,
  onBack,
  refreshSession,
}: {
  enabled: boolean;
  onBack: () => void;
  refreshSession: () => Promise<SessionResponse>;
}) {
  const t = useT();
  const containerRef = useRef<HTMLDivElement | null>(null);
  const socketRef = useRef<WebSocket | null>(null);
  const terminalRef = useRef<Terminal | null>(null);
  const fitAddonRef = useRef<FitAddon | null>(null);
  const [status, setStatus] = useState<"idle" | "connecting" | "connected">("idle");
  const [notice, setNoticeState] = useState<string | null>(null);
  const [noticeSource, setNoticeSource] = useState<string | null>(null);

  function setNotice(message: string | null, source?: string) {
    setNoticeState(message);
    setNoticeSource(message === null ? null : source ?? message);
  }

  useEffect(() => {
    return () => {
      socketRef.current?.close();
      terminalRef.current?.dispose();
      socketRef.current = null;
      terminalRef.current = null;
      fitAddonRef.current = null;
    };
  }, []);

  useEffect(() => {
    if (status === "idle" || !containerRef.current || terminalRef.current) {
      return;
    }

    let disposed = false;
    let term: Terminal | null = null;
    let fitAddon: FitAddon | null = null;
    let removeResizeListener: (() => void) | null = null;

    void Promise.all([import("@xterm/xterm"), import("@xterm/addon-fit")]).then(([xtermModule, fitModule]) => {
      if (disposed || !containerRef.current || terminalRef.current) {
        return;
      }

      term = new xtermModule.Terminal({
        allowTransparency: true,
        cursorBlink: true,
        fontFamily: '"Menlo", "SFMono-Regular", "Consolas", monospace',
        fontSize: 13,
        theme: {
          background: "#0f160e",
          foreground: "#e8f1e3",
        },
      });
      fitAddon = new fitModule.FitAddon();

      const handleResize = () => {
        if (!fitAddonRef.current || !terminalRef.current) {
          return;
        }

        fitAddonRef.current.fit();
        if (socketRef.current?.readyState === WebSocket.OPEN) {
          socketRef.current.send(
            JSON.stringify({
              type: "resize",
              cols: terminalRef.current.cols,
              rows: terminalRef.current.rows,
            }),
          );
        }
      };

      terminalRef.current = term;
      fitAddonRef.current = fitAddon;
      term.loadAddon(fitAddon);
      term.open(containerRef.current);
      fitAddon.fit();
      term.writeln(t("Central Scrutinizer terminal"));
      term.writeln("");
      term.onData((data) => {
        if (socketRef.current?.readyState === WebSocket.OPEN) {
          socketRef.current.send(data);
        }
      });
      window.addEventListener("resize", handleResize);
      removeResizeListener = () => {
        window.removeEventListener("resize", handleResize);
      };

      if (socketRef.current?.readyState === WebSocket.OPEN) {
        handleResize();
      }
    });

    return () => {
      disposed = true;
      if (removeResizeListener) {
        removeResizeListener();
      }
      if (term) {
        term.dispose();
      }
      terminalRef.current = null;
      fitAddonRef.current = null;
    };
  }, [status]);

  useEffect(() => {
    if (enabled) {
      return;
    }

    socketRef.current?.close();
    socketRef.current = null;
    setStatus("idle");
  }, [enabled]);

  async function handleConnect() {
    let nextSession: SessionResponse;
    let socket: WebSocket;
    let baseUrl: URL;

    setNotice(null);
    setStatus("connecting");

    try {
      nextSession = await refreshSession();
      if (!nextSession.capabilities.terminal) {
        setNotice(t("Terminal access is disabled on the handheld."), "Terminal access is disabled on the handheld.");
        setStatus("idle");
        return;
      }
      if (!nextSession.csrf) {
        throw new Error("Missing session csrf token.");
      }

      const { ticket } = await createTerminalSession(nextSession.csrf);
      baseUrl = new URL(window.location.href);
      socket = new WebSocket(
        `${baseUrl.protocol === "https:" ? "wss" : "ws"}://${baseUrl.host}/api/terminal/socket`,
      );
      socket.binaryType = "arraybuffer";
      socketRef.current = socket;

      socket.addEventListener("open", () => {
        socket.send(JSON.stringify({ type: "auth", ticket }));
      });
      socket.addEventListener("message", (event) => {
        if (typeof event.data === "string") {
          try {
            const message = JSON.parse(event.data) as { type?: string; error?: string };

            if (message.type === "ready") {
              setStatus("connected");
              if (fitAddonRef.current && terminalRef.current) {
                fitAddonRef.current.fit();
                socket.send(
                  JSON.stringify({
                    type: "resize",
                    cols: terminalRef.current.cols,
                    rows: terminalRef.current.rows,
                  }),
                );
              }
              return;
            }
            if (message.type === "error") {
              setNotice(describeTerminalError(message.error ?? "", t), message.error ?? "Terminal connection failed.");
              socket.close();
              return;
            }
            if (message.type === "closed") {
              setNotice(t("Terminal session closed."), "Terminal session closed.");
              socket.close();
            }
          } catch {
            // Ignore non-JSON text frames.
          }
          return;
        }

        if (event.data instanceof ArrayBuffer) {
          terminalRef.current?.write(new Uint8Array(event.data));
        }
      });
      socket.addEventListener("close", () => {
        if (socketRef.current === socket) {
          socketRef.current = null;
        }
        setStatus("idle");
      });
      socket.addEventListener("error", () => {
        setNotice(t("Terminal connection failed."), "Terminal connection failed.");
        socket.close();
      });
    } catch (error) {
      setNotice(error instanceof Error ? t(error.message) : t("Terminal connection failed."), error instanceof Error ? error.message : "Terminal connection failed.");
      setStatus("idle");
    }
  }

  function handleDisconnect() {
    socketRef.current?.close();
    socketRef.current = null;
    setStatus("idle");
  }

  const terminalActive = status === "connecting" || status === "connected";

  return (
    <div className="flex h-full min-h-0 flex-col gap-4 md:gap-5">
      <button
        className="inline-flex shrink-0 items-center gap-2 self-start text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
        onClick={() => {
          handleDisconnect();
          onBack();
        }}
        type="button"
      >
        <span aria-hidden="true">←</span>
        {t("Back")}
      </button>
      <div className="shrink-0 space-y-2">
        <h2 className="text-lg font-semibold">{t("Terminal")}</h2>
        {!terminalActive ? (
          <p className="text-sm text-[var(--muted)]">
            {t("This opens a real shell on the device. Use it only when you understand the commands you are running.")}
          </p>
        ) : null}
      </div>
      {!enabled ? (
        <div className="rounded-2xl border border-[var(--border)] bg-[var(--panel)] px-5 py-5 text-sm text-[var(--muted)]">
          {t("Terminal access is disabled on the handheld. Enable it from the device settings screen first.")}
        </div>
      ) : (
        <>
          {notice ? <NoticeToast message={notice} source={noticeSource ?? notice} onDismiss={() => setNotice(null)} /> : null}
          {status === "idle" ? (
            <div className="shrink-0 rounded-2xl border border-[var(--border)] bg-[var(--panel)] px-5 py-5">
              <p className="text-sm text-[var(--muted)]">
                {t("The session starts in the SD card root and uses the device shell through a PTY-backed websocket.")}
              </p>
              <button
                className="mt-4 rounded-full border border-[var(--border)] bg-[var(--accent-soft)] px-4 py-2 text-sm font-semibold text-[var(--accent)] transition hover:border-[var(--accent)]/40"
                onClick={() => {
                  void handleConnect();
                }}
                type="button"
              >
                {t("Acknowledge And Connect")}
              </button>
            </div>
          ) : null}
          {terminalActive ? (
            <div className="flex min-h-0 flex-1 flex-col gap-3">
              <div className="flex shrink-0 items-center justify-between gap-3">
                <p className="text-sm text-[var(--muted)]">
                  {status === "connecting" ? t("Connecting terminal...") : t("Connected")}
                </p>
                <button
                  className="rounded-full border border-[var(--border)] px-4 py-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
                  onClick={handleDisconnect}
                  type="button"
                >
                  {t("Disconnect")}
                </button>
              </div>
              <div className="min-h-[18rem] flex-1 rounded-2xl border border-[var(--border)] bg-black/40 p-3">
                <div className="h-full min-h-[16rem]" ref={containerRef} />
              </div>
            </div>
          ) : null}
        </>
      )}
    </div>
  );
}
