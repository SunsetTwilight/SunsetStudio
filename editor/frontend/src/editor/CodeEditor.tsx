// ============================================================
//  Monaco Editor の React の部品 (@monaco-editor/react は CDN から読みに行くので使わない)
//
//    value       最初の内容 (key を変えると作り直す。外から差し替えるときは setValue を使う)
//    onChange    内容が変わった
//    onSave      Ctrl+S
//    markers     エラー / 警告の印 (行、列は 1 から)
//    api         外から操作する (setValue / revealLine / focus)
// ============================================================
import { useEffect, useRef } from 'react';
import { monaco, setupMonaco } from './monaco';

export interface EditorMarker {
  line: number;
  column: number;
  severity: 'error' | 'warning' | 'note';
  message: string;
}

export interface CodeEditorApi {
  setValue(text: string): void;
  getValue(): string;
  revealLine(line: number, column?: number): void;
  focus(): void;
}

interface Props {
  value: string;
  language: string;
  markers?: EditorMarker[];
  onChange?: (text: string) => void;
  onSave?: () => void;
  api?: (api: CodeEditorApi | null) => void;
}

const SEVERITY = { error: 8, warning: 4, note: 2 } as const; // monaco.MarkerSeverity

export function CodeEditor({ value, language, markers, onChange, onSave, api }: Props) {
  const host = useRef<HTMLDivElement>(null);
  const editor = useRef<monaco.editor.IStandaloneCodeEditor | null>(null);
  const handlers = useRef({ onChange, onSave });
  handlers.current = { onChange, onSave };

  useEffect(() => {
    if (!host.current) return;
    setupMonaco();
    const ed = monaco.editor.create(host.current, {
      value,
      language,
      theme: 'sunset-dark',
      automaticLayout: true,
      fontSize: 13,
      tabSize: 4,
      insertSpaces: false,
      minimap: { enabled: false },
      scrollBeyondLastLine: false,
      renderWhitespace: 'selection',
      fixedOverflowWidgets: true,
      // 文字入力は textarea 方式にする (EditContext は WebView2 での IME の実績が少ない)
      editContext: false,
    });
    editor.current = ed;
    const sub = ed.onDidChangeModelContent(() => handlers.current.onChange?.(ed.getValue()));
    ed.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, () => handlers.current.onSave?.());
    api?.({
      setValue: (text) => {
        if (ed.getValue() !== text) ed.setValue(text);
      },
      getValue: () => ed.getValue(),
      revealLine: (line, column = 1) => {
        ed.revealLineInCenter(line);
        ed.setPosition({ lineNumber: line, column });
        ed.focus();
      },
      focus: () => ed.focus(),
    });
    return () => {
      api?.(null);
      sub.dispose();
      ed.getModel()?.dispose();
      ed.dispose();
      editor.current = null;
    };
    // 内容と言語は作るときだけ使う (作り直さない)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    const model = editor.current?.getModel();
    if (!model) return;
    monaco.editor.setModelMarkers(
      model,
      'sunset',
      (markers ?? []).map((m) => ({
        startLineNumber: m.line,
        startColumn: m.column,
        endLineNumber: m.line,
        endColumn: model.getLineMaxColumn(Math.min(Math.max(m.line, 1), model.getLineCount())),
        severity: SEVERITY[m.severity],
        message: m.message,
      })),
    );
  }, [markers]);

  return <div className="code-editor" ref={host} />;
}
