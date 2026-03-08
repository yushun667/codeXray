import React, { useState, useEffect, useRef } from 'react';
import ReactMarkdown from 'react-markdown';
import remarkGfm from 'remark-gfm';
import { Codicon } from '../shared/icons';

interface ChatTabProps {
  onMessage: ((msg: unknown) => void) | null;
}

interface Message {
  role: 'user' | 'assistant';
  content: string;
}

export function ChatTab({ onMessage }: ChatTabProps) {
  const [input, setInput] = useState('');
  const [messages, setMessages] = useState<Message[]>([]);
  const [streaming, setStreaming] = useState('');
  const listRef = useRef<HTMLDivElement>(null);
  const streamingRef = useRef('');

  useEffect(() => {
    streamingRef.current = streaming;
  }, [streaming]);

  useEffect(() => {
    const handler = (event: MessageEvent<{ action?: string; full?: string; chunk?: string; context?: { symbol?: string } }>) => {
      const m = event.data;
      if (!m || typeof m !== 'object') return;
      if (m.action === 'chatReply' && m.full) {
        setMessages((prev) => [...prev, { role: 'assistant', content: m.full! }]);
        setStreaming('');
      }
      if (m.action === 'replyChunk' && m.chunk) {
        setStreaming((s) => s + m.chunk);
      }
      if (m.action === 'replyDone') {
        const current = streamingRef.current;
        if (current) {
          setMessages((prev) => [...prev, { role: 'assistant', content: current }]);
          setStreaming('');
        }
      }
      if (m.action === 'context' && m.context?.symbol) {
        setInput((i) => i || `当前符号: ${m.context!.symbol}`);
      }
    };
    window.addEventListener('message', handler);
    return () => window.removeEventListener('message', handler);
  }, []);

  useEffect(() => {
    listRef.current?.scrollTo(0, listRef.current.scrollHeight);
  }, [messages, streaming]);

  const send = () => {
    const msg = input.trim();
    if (!msg || !onMessage) return;
    onMessage({ action: 'sendChat', payload: { message: msg } });
    setMessages((prev) => [...prev, { role: 'user', content: msg }]);
    setInput('');
  };

  return (
    <div className="panel chat-panel">
      <div className="message-list" ref={listRef}>
        {messages.map((msg, i) => (
          <div key={i} className={`msg ${msg.role}`}>
            <ReactMarkdown remarkPlugins={[remarkGfm]}>{msg.content}</ReactMarkdown>
          </div>
        ))}
        {streaming && (
          <div className="msg assistant streaming">
            <ReactMarkdown remarkPlugins={[remarkGfm]}>{streaming}</ReactMarkdown>
          </div>
        )}
      </div>
      <div className="section">
        <button className="btn" onClick={() => onMessage?.({ action: 'getContext' })}>
          <span className={Codicon['symbol-reference']} /> 引用当前符号
        </button>
      </div>
      <textarea
        className="chat-input"
        rows={3}
        placeholder="输入消息…"
        value={input}
        onChange={(e) => setInput(e.target.value)}
        onKeyDown={(e) => e.key === 'Enter' && !e.shiftKey && (e.preventDefault(), send())}
      />
      <button className="btn" onClick={send}>
        <span className={Codicon.send} /> 发送
      </button>
    </div>
  );
}
