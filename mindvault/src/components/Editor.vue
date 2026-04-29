<template>
  <div class="editor-container">
    <div class="editor-header">
      <div class="note-title">{{ currentNote.title }}</div>
      <div class="editor-actions">
        <button 
          class="mode-btn" 
          :class="{ active: mode === 'source' }"
          @click="setMode('source')"
        >
          源码
        </button>
        <button 
          class="mode-btn" 
          :class="{ active: mode === 'preview' }"
          @click="setMode('preview')"
        >
          预览
        </button>
      </div>
    </div>
    
    <div class="editor-body">
      <div ref="editorRef" class="codemirror-wrapper"></div>
    </div>
    
    <div class="editor-footer">
      <span>{{ wordCount }} 字</span>
      <span>{{ saveStatus }}</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue';
import { EditorView, keymap } from '@codemirror/view';
import { EditorState } from '@codemirror/state';
import { markdown } from '@codemirror/lang-markdown';
import { oneDark } from '@codemirror/theme-one-dark';
import { defaultKeymap, history, historyKeymap } from '@codemirror/commands';
import { searchKeymap, highlightSelectionMatches } from '@codemirror/search';

const editorRef = ref<HTMLDivElement>();
const mode = ref<'source' | 'preview'>('source');
const wordCount = ref(0);
const saveStatus = ref('已保存');

const currentNote = ref({
  id: '1',
  title: '欢迎使用 MindVault',
  content: '# 欢迎使用 MindVault\n\n这是一个本地知识库桌面客户端。\n\n## 特性\n\n- 📝 Markdown 编辑\n- 🔍 全文检索\n- 🔗 双向链接\n- 🤖 AI 增强\n\n开始你的知识管理之旅吧！'
});

let editorView: EditorView | null = null;

const createEditor = () => {
  if (!editorRef.value) return;

  const state = EditorState.create({
    doc: currentNote.value.content,
    extensions: [
      markdown(),
      oneDark,
      history(),
      highlightSelectionMatches(),
      keymap.of([
        ...defaultKeymap,
        ...historyKeymap,
        ...searchKeymap,
      ]),
      EditorView.updateListener.of((update) => {
        if (update.docChanged) {
          const content = update.state.doc.toString();
          wordCount.value = content.length;
          saveStatus.value = '未保存';
          
          // 自动保存（防抖）
          debounceSave(content);
        }
      }),
      EditorView.theme({
        '&': {
          height: '100%',
        },
        '.cm-scroller': {
          fontFamily: "'JetBrains Mono', 'Fira Code', monospace",
          fontSize: '14px',
        },
        '.cm-content': {
          padding: '20px',
        },
      }),
    ],
  });

  editorView = new EditorView({
    state,
    parent: editorRef.value,
  });
};

let saveTimeout: ReturnType<typeof setTimeout> | null = null;

const debounceSave = (content: string) => {
  if (saveTimeout) clearTimeout(saveTimeout);
  saveTimeout = setTimeout(() => {
    // TODO: 实际保存逻辑
    console.log('自动保存:', content.substring(0, 50) + '...');
    saveStatus.value = '已保存';
  }, 1000);
};

const setMode = (newMode: 'source' | 'preview') => {
  mode.value = newMode;
  // TODO: 切换预览模式
};

onMounted(() => {
  createEditor();
  wordCount.value = currentNote.value.content.length;
});

onUnmounted(() => {
  if (editorView) {
    editorView.destroy();
  }
  if (saveTimeout) {
    clearTimeout(saveTimeout);
  }
});
</script>

<style scoped>
.editor-container {
  flex: 1;
  display: flex;
  flex-direction: column;
  background: #1e1e1e;
}

.editor-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 20px;
  border-bottom: 1px solid #333;
  background: #1a1a1a;
}

.note-title {
  font-size: 16px;
  font-weight: 500;
  color: #fff;
}

.editor-actions {
  display: flex;
  gap: 8px;
}

.mode-btn {
  padding: 6px 12px;
  background: transparent;
  border: 1px solid #333;
  border-radius: 4px;
  color: #999;
  cursor: pointer;
  font-size: 13px;
  transition: all 0.2s;
}

.mode-btn:hover {
  border-color: #555;
  color: #fff;
}

.mode-btn.active {
  background: #0ea5e9;
  border-color: #0ea5e9;
  color: #fff;
}

.editor-body {
  flex: 1;
  overflow: hidden;
}

.codemirror-wrapper {
  height: 100%;
}

.editor-footer {
  display: flex;
  justify-content: space-between;
  padding: 8px 20px;
  border-top: 1px solid #333;
  background: #1a1a1a;
  font-size: 12px;
  color: #666;
}
</style>
