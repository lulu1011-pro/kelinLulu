<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount, shallowRef, nextTick } from 'vue'
import { EditorState } from '@codemirror/state'
import { EditorView, keymap } from '@codemirror/view'
import { defaultKeymap, history, historyKeymap, indentWithTab } from '@codemirror/commands'
import { markdown, markdownLanguage } from '@codemirror/lang-markdown'
import { oneDark } from '@codemirror/theme-one-dark'
import { searchKeymap } from '@codemirror/search'
import { type Note, notesApi, tagsApi, filesApi } from '../api'
import { marked } from 'marked'
import TurndownService from 'turndown'
import mammoth from 'mammoth'
import OutlinePanel from './OutlinePanel.vue'
import VersionHistory from './VersionHistory.vue'

const props = defineProps<{ note: Note }>()
const emit = defineEmits<{
  save: [id: number, title: string, content: string]
  navigate: [title: string]
  openSettings: []
}>()

const editMode = ref<'edit' | 'preview' | 'split'>('split')
const showOutline = ref(true)
const showVersionHistory = ref(false)
const noteTitle = ref(props.note.title)
const noteContent = ref(props.note.content || '')
const previewHtml = ref('')
let saveTimer: ReturnType<typeof setTimeout> | null = null
const editorContainer = ref<HTMLDivElement>()
const previewRef = ref<HTMLDivElement>()
const editorView = shallowRef<EditorView>()
let renderTimer: ReturnType<typeof setTimeout> | null = null

// ─── Tags ───
const noteTags = ref<{ id: number; name: string }[]>([])
const newTagInput = ref('')
const showTagInput = ref(false)
const tagInputRef = ref<HTMLInputElement>()

async function loadTags() {
  try {
    noteTags.value = await tagsApi.getNoteTags(props.note.id)
  } catch (e) {
    console.error('Failed to load tags:', e)
  }
}

async function addTag() {
  const name = newTagInput.value.trim()
  if (!name) return
  try {
    await tagsApi.addToNote(props.note.id, name)
    await loadTags()
    newTagInput.value = ''
    showTagInput.value = false
  } catch (e) {
    console.error('Failed to add tag:', e)
  }
}

async function removeTag(tagName: string) {
  try {
    await tagsApi.removeFromNote(props.note.id, tagName)
    await loadTags()
  } catch (e) {
    console.error('Failed to remove tag:', e)
  }
}

function startAddTag() {
  showTagInput.value = true
  newTagInput.value = ''
  nextTick(() => tagInputRef.value?.focus())
}

function onTagKeydown(e: KeyboardEvent) {
  if (e.key === 'Enter') {
    e.preventDefault()
    addTag()
  } else if (e.key === 'Escape') {
    showTagInput.value = false
    newTagInput.value = ''
  }
}

// ─── Turndown: HTML → Markdown 转换器 ───
const turndown = new TurndownService({
  headingStyle: 'atx',
  bulletListMarker: '-',
  codeBlockStyle: 'fenced',
})
turndown.addRule('strikethrough', {
  filter: ['del', 's'],
  replacement: (content) => `~~${content}~~`,
})

// ─── Marked: 用 try-catch 保护初始化 ───
let markedReady = false
try {
  const wikiLinkExt = {
    name: 'wikiLink',
    level: 'inline' as const,
    start(src: string) { return src.indexOf('[[') },
    tokenizer(src: string) {
      const m = src.match(/^\[\[([^\]]+?)\]\]/)
      if (m) return { type: 'wikiLink', raw: m[0], pageTitle: m[1] }
    },
    renderer(token: any) {
      const t = token.pageTitle as string
      return `<a href="wiki://${encodeURIComponent(t)}" class="wiki-link">${t}</a>`
    },
  }
  marked.use({ extensions: [wikiLinkExt] })
  markedReady = true
} catch (e) {
  console.warn('marked wiki extension failed:', e)
  markedReady = false
}

function renderMarkdown(content: string): string {
  if (!content) return '<p class="empty-preview">Start writing...</p>'
  try {
    return marked.parse(content) as string
  } catch (e) {
    console.warn('marked parse error:', e)
    return `<pre>${content}</pre>`
  }
}

function renderPreviewNow() {
  previewHtml.value = renderMarkdown(noteContent.value)
}

function schedulePreviewRender() {
  if (renderTimer) clearTimeout(renderTimer)
  renderTimer = setTimeout(renderPreviewNow, 200)
}

function scheduleSave() {
  if (saveTimer) clearTimeout(saveTimer)
  saveTimer = setTimeout(() => {
    emit('save', props.note.id, noteTitle.value, noteContent.value)
  }, 800)
}

// ─── Markdown 工具栏操作 ───

/** 在编辑器中包裹选区或插入语法 */
function wrapSelection(before: string, after: string, placeholder = '') {
  const view = editorView.value
  if (!view) return
  const { from, to } = view.state.selection.main
  const selected = view.state.sliceDoc(from, to)
  const text = selected || placeholder
  view.dispatch({
    changes: { from, to, insert: `${before}${text}${after}` },
    selection: selected
      ? { anchor: from, head: from + before.length + text.length + after.length }
      : { anchor: from + before.length, head: from + before.length + text.length },
  })
  view.focus()
}

/** 在行首插入前缀（如标题、列表） */
function prependLine(prefix: string) {
  const view = editorView.value
  if (!view) return
  const { from, to } = view.state.selection.main
  const line = view.state.doc.lineAt(from)
  const currentPrefix = view.state.sliceDoc(line.from, line.from + prefix.length)
  const changes = currentPrefix === prefix
    ? { from: line.from, to: line.from + prefix.length, insert: '' }  // toggle off
    : { from: line.from, to: line.from, insert: prefix }
  view.dispatch({ changes })
  view.focus()
}

/** 在光标下方插入块（代码块、表格等） */
function insertBlock(block: string) {
  const view = editorView.value
  if (!view) return
  const { from } = view.state.selection.main
  const lineBreak = view.state.doc.lineAt(from).text ? '\n' : ''
  view.dispatch({
    changes: { from, to: from, insert: `${lineBreak}${block}` },
    selection: { anchor: from + lineBreak.length + block.length },
  })
  view.focus()
}

const toolbarActions = {
  bold: () => wrapSelection('**', '**', '粗体'),
  italic: () => wrapSelection('*', '*', '斜体'),
  strikethrough: () => wrapSelection('~~', '~~', '删除线'),
  code: () => wrapSelection('`', '`', 'code'),
  h1: () => prependLine('# '),
  h2: () => prependLine('## '),
  h3: () => prependLine('### '),
  ul: () => prependLine('- '),
  ol: () => prependLine('1. '),
  task: () => prependLine('- [ ] '),
  quote: () => prependLine('> '),
  link: () => wrapSelection('[', '](url)', '链接文字'),
  image: () => wrapSelection('![', '](url)', '图片描述'),
  codeBlock: () => insertBlock('\n```\n代码\n```\n'),
  table: () => insertBlock('\n| 列1 | 列2 | 列3 |\n| --- | --- | --- |\n| 内容 | 内容 | 内容 |\n'),
  hr: () => insertBlock('\n---\n'),
}

// ─── 导入文件（HTML → Markdown） ───
const importInput = ref<HTMLInputElement>()

async function handleImportFile(e: Event) {
  const file = (e.target as HTMLInputElement).files?.[0]
  if (!file) return

  const ext = file.name.split('.').pop()?.toLowerCase()

  if (ext === 'docx') {
    // Word → HTML → Markdown
    const arrayBuffer = await file.arrayBuffer()
    const result = await mammoth.convertToHtml({ arrayBuffer })
    const html = result.value
    const md = turndown.turndown(html)
    setContent(md)
  } else if (ext === 'md' || ext === 'txt') {
    // Markdown/纯文本直接导入
    const text = await file.text()
    setContent(text)
  } else if (ext === 'html' || ext === 'htm') {
    // HTML → Markdown
    const html = await file.text()
    const md = turndown.turndown(html)
    setContent(md)
  } else {
    // 尝试作为 HTML 处理（如从网页复制的 .doc 实际是 HTML）
    const text = await file.text()
    if (text.includes('<html') || text.includes('<p>') || text.includes('<div')) {
      const md = turndown.turndown(text)
      setContent(md)
    } else {
      // 纯文本兜底
      setContent(text)
    }
  }

  // 重置 input 以便重复选择同一文件
  ;(e.target as HTMLInputElement).value = ''
}

function setContent(md: string) {
  noteContent.value = md
  if (editorView.value) {
    editorView.value.dispatch({
      changes: { from: 0, to: editorView.value.state.doc.length, insert: md },
    })
  }
  renderPreviewNow()
  scheduleSave()
}

// ─── 从剪贴板粘贴 HTML（自动转 Markdown） ───
async function handlePaste(e: ClipboardEvent) {
  // 检查是否有图片
  const items = e.clipboardData?.items
  if (items) {
    for (const item of items) {
      if (item.type.startsWith('image/')) {
        e.preventDefault()
        const file = item.getAsFile()
        if (file) {
          await uploadAndInsertImage(file)
        }
        return
      }
    }
  }

  // 检查 HTML 内容
  const html = e.clipboardData?.getData('text/html')
  if (html && html.trim()) {
    e.preventDefault()
    const md = turndown.turndown(html)
    const view = editorView.value
    if (view) {
      const { from, to } = view.state.selection.main
      view.dispatch({ changes: { from, to, insert: md } })
      view.focus()
    }
  }
}

// ─── 上传图片并插入 Markdown ───
async function uploadAndInsertImage(file: File) {
  try {
    const { url } = await filesApi.uploadImage(file)
    const md = `![${file.name}](${url})`
    const view = editorView.value
    if (view) {
      const { from, to } = view.state.selection.main
      view.dispatch({ changes: { from, to, insert: md } })
      view.focus()
    }
  } catch (e) {
    console.error('Image upload failed:', e)
  }
}

// ─── 拖拽图片处理 ───
function handleDrop(e: DragEvent) {
  const files = e.dataTransfer?.files
  if (!files) return

  for (const file of files) {
    if (file.type.startsWith('image/')) {
      e.preventDefault()
      uploadAndInsertImage(file)
    }
  }
}

// ─── PDF 导出 ───
function exportPdf() {
  const el = previewRef.value
  if (!el) return

  // 创建一个干净的打印窗口
  const printWin = window.open('', '_blank')
  if (!printWin) return

  const styles = `
    <style>
      body {
        font-family: -apple-system, 'Segoe UI', sans-serif;
        line-height: 1.75;
        color: #1a1b26;
        max-width: 800px;
        margin: 0 auto;
        padding: 40px 32px;
        background: #fff;
      }
      h1 { font-size: 28px; margin: 28px 0 14px; padding-bottom: 10px; border-bottom: 1px solid #e0e0e0; }
      h2 { font-size: 22px; margin: 24px 0 12px; }
      h3 { font-size: 18px; margin: 20px 0 10px; }
      h4 { font-size: 16px; margin: 16px 0 8px; }
      p { margin: 10px 0; }
      a { color: #3b82f6; text-decoration: none; }
      .wiki-link { color: #8b5cf6; background: #f3f0ff; padding: 1px 6px; border-radius: 3px; }
      code { background: #f1f5f9; padding: 2px 6px; border-radius: 4px; font-size: 13px; font-family: 'JetBrains Mono', Consolas, monospace; color: #16a34a; }
      pre { background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 8px; padding: 16px; overflow-x: auto; margin: 14px 0; }
      pre code { background: transparent; padding: 0; color: #475569; }
      blockquote { border-left: 3px solid #3b82f6; margin: 14px 0; padding: 6px 18px; color: #64748b; background: #eff6ff; border-radius: 0 4px 4px 0; }
      ul, ol { padding-left: 24px; margin: 10px 0; }
      li { margin: 4px 0; }
      hr { border: none; border-top: 1px solid #e2e8f0; margin: 24px 0; }
      table { border-collapse: collapse; width: 100%; margin: 14px 0; }
      th, td { border: 1px solid #e2e8f0; padding: 8px 14px; text-align: left; }
      th { background: #f8fafc; font-weight: 600; }
      img { max-width: 100%; border-radius: 8px; }
      @media print {
        body { padding: 20px; }
        a { color: #1a1b26; text-decoration: underline; }
      }
    </style>
  `

  printWin.document.write(`<!DOCTYPE html><html><head><title>${noteTitle.value}</title>${styles}</head><body>${previewHtml.value}</body></html>`)
  printWin.document.close()
  printWin.focus()

  setTimeout(() => {
    printWin.print()
  }, 300)
}

// ─── 大纲：滚动同步 ───
const activeOutlineSlug = ref('')
let scrollSyncTimer: ReturnType<typeof setTimeout> | null = null

function handlePreviewScroll() {
  if (scrollSyncTimer) clearTimeout(scrollSyncTimer)
  scrollSyncTimer = setTimeout(() => {
    if (!previewRef.value) return
    const headings = previewRef.value.querySelectorAll('h1, h2, h3, h4, h5, h6')
    if (headings.length === 0) return

    const scrollTop = previewRef.value.scrollTop
    const containerTop = previewRef.value.getBoundingClientRect().top

    // 找到当前可见的标题（最后一个在视口上方的标题）
    let currentSlug = ''
    for (const h of headings) {
      const rect = h.getBoundingClientRect()
      const relativeTop = rect.top - containerTop
      if (relativeTop <= 60) {  // 60px 容差
        const text = h.textContent || ''
        currentSlug = text
          .toLowerCase()
          .replace(/[^\w\u4e00-\u9fff\s-]/g, '')
          .replace(/\s+/g, '-')
          .replace(/-+/g, '-')
          .replace(/^-|-$/g, '')
      }
    }
    activeOutlineSlug.value = currentSlug
  }, 80)
}

// ─── 大纲：滚动到标题 ───
function handleOutlineScroll(slug: string) {
  if (!previewRef.value) return
  // 在预览区查找对应的标题元素
  const headings = previewRef.value.querySelectorAll('h1, h2, h3, h4, h5, h6')
  for (const h of headings) {
    const hSlug = h.textContent
      ?.toLowerCase()
      .replace(/[^\w\u4e00-\u9fff\s-]/g, '')
      .replace(/\s+/g, '-')
      .replace(/-+/g, '-')
      .replace(/^-|-$/g, '') || ''
    if (hSlug === slug) {
      h.scrollIntoView({ behavior: 'smooth', block: 'start' })
      // 高亮闪烁效果
      h.classList.add('outline-highlight')
      setTimeout(() => h.classList.remove('outline-highlight'), 1500)
      break
    }
  }
}

// ─── 版本恢复 ───
function handleVersionRestore(content: string) {
  setContent(content)
}

// ─── Editor 创建 ───
function createEditor() {
  if (!editorContainer.value) return
  if (editorView.value) editorView.value.destroy()

  try {
    const startState = EditorState.create({
      doc: noteContent.value,
      extensions: [
        EditorView.lineWrapping,
        history(),
        markdown({ base: markdownLanguage }),
        oneDark,
        keymap.of([
          ...defaultKeymap,
          ...historyKeymap,
          ...searchKeymap,
          indentWithTab,
          // 快捷键：Ctrl+B 粗体, Ctrl+I 斜体
          { key: 'Mod-b', run: () => { toolbarActions.bold(); return true } },
          { key: 'Mod-i', run: () => { toolbarActions.italic(); return true } },
          { key: 'Mod-k', run: () => { toolbarActions.link(); return true } },
          { key: 'Mod-Shift-s', run: () => { toolbarActions.strikethrough(); return true } },
        ]),
        EditorView.updateListener.of((update) => {
          if (update.docChanged) {
            noteContent.value = update.state.doc.toString()
            schedulePreviewRender()
            scheduleSave()
          }
        }),
        EditorView.theme({
          '&': { height: '100%', fontSize: '14px' },
          '.cm-scroller': { overflow: 'auto' },
          '.cm-content': { fontFamily: '"JetBrains Mono", Consolas, monospace' },
        }),
        // 拦截粘贴和拖拽事件
        EditorView.domEventHandlers({
          paste: handlePaste,
          drop: handleDrop,
        }),
      ],
    })

    editorView.value = new EditorView({
      state: startState,
      parent: editorContainer.value,
    })

    // 编辑器滚动同步大纲
    const cmScroller = editorView.value.scrollDOM
    cmScroller.addEventListener('scroll', handleEditorScroll)
  } catch (e) {
    console.error('CodeMirror init failed:', e)
  }
}

function handleEditorScroll() {
  if (scrollSyncTimer) clearTimeout(scrollSyncTimer)
  scrollSyncTimer = setTimeout(() => {
    if (!editorView.value || editMode.value === 'preview') return
    const scroller = editorView.value.scrollDOM
    const scrollTop = scroller.scrollTop
    const lines = editorView.value.state.doc
    const lineHeight = editorView.value.defaultLineHeight

    // 找到当前可见区域对应的行
    let currentSlug = ''
    for (let i = 1; i <= lines.lines; i++) {
      const line = lines.line(i)
      const linePos = editorView.value.lineBlockAt(line.from)
      const lineTop = linePos.top
      if (lineTop > scrollTop + 60) break

      const text = line.text
      const match = text.match(/^(#{1,6})\s+(.+)$/)
      if (match) {
        currentSlug = match[2].trim()
          .toLowerCase()
          .replace(/[^\w\u4e00-\u9fff\s-]/g, '')
          .replace(/\s+/g, '-')
          .replace(/-+/g, '-')
          .replace(/^-|-$/g, '')
      }
    }
    activeOutlineSlug.value = currentSlug
  }, 80)
}

function handlePreviewClick(e: MouseEvent) {
  const target = e.target as HTMLElement
  const anchor = target.closest('a') as HTMLAnchorElement | null
  if (!anchor) return

  const href = anchor.getAttribute('href') || ''

  // Wiki 链接：wiki://标题
  if (href.startsWith('wiki://')) {
    e.preventDefault()
    e.stopPropagation()
    const title = decodeURIComponent(href.slice(7))
    emit('navigate', title)
    return
  }

  // 锚点链接：#heading
  if (href.startsWith('#')) {
    e.preventDefault()
    const id = href.slice(1)
    const el = previewRef.value?.querySelector(`[id="${id}"]`) || previewRef.value?.querySelector(`a[id="${id}"]`)
    el?.scrollIntoView({ behavior: 'smooth', block: 'start' })
    return
  }

  // 外部链接：http/https
  if (href.startsWith('http://') || href.startsWith('https://')) {
    e.preventDefault()
    window.open(href, '_blank', 'noopener,noreferrer')
    return
  }

  // 其他链接：也阻止默认行为
  if (href) {
    e.preventDefault()
  }
}

async function navigateToWikiLink(title: string) {
  // 通过 emit 让 App.vue 负责切换笔记（而不是内部偷偷改）
  emit('navigate', title)
}

watch(() => props.note.id, () => {
  noteTitle.value = props.note.title
  noteContent.value = props.note.content || ''
  activeOutlineSlug.value = ''
  if (editorView.value) {
    editorView.value.dispatch({
      changes: { from: 0, to: editorView.value.state.doc.length, insert: noteContent.value }
    })
  }
  renderPreviewNow()
  loadTags()
})

watch(noteTitle, () => scheduleSave())

onMounted(() => {
  renderPreviewNow()
  loadTags()
  setTimeout(createEditor, 50)
})

onBeforeUnmount(() => {
  if (editorView.value) editorView.value.destroy()
  if (saveTimer) clearTimeout(saveTimer)
  if (renderTimer) clearTimeout(renderTimer)
  if (scrollSyncTimer) clearTimeout(scrollSyncTimer)
})
</script>

<template>
  <div class="editor-wrapper">
    <!-- ─── 标题栏 ─── -->
    <div class="editor-toolbar">
      <input v-model="noteTitle" class="note-title-input" placeholder="Note title..." />
      <div class="toolbar-right">
        <button class="btn-toolbar" @click="exportPdf" title="导出 PDF">📄</button>
        <button class="btn-toolbar" @click="importInput?.click()" title="导入文件 (HTML/TXT/MD)">📥</button>
        <button
          class="btn-toolbar"
          :class="{ active: showOutline }"
          @click="showOutline = !showOutline"
          title="大纲导航"
        >📑</button>
        <button class="btn-toolbar" @click="emit('openSettings')" title="设置">⚙️</button>
        <button class="btn-toolbar" @click="showVersionHistory = true" title="版本历史">📋</button>
        <input ref="importInput" type="file" accept=".md,.txt,.html,.htm,.docx" style="display:none" @change="handleImportFile" />
        <div class="mode-switch">
          <button :class="{ active: editMode === 'edit' }" @click="editMode = 'edit'" title="编辑模式">✏️</button>
          <button :class="{ active: editMode === 'split' }" @click="editMode = 'split'" title="分栏模式">📖</button>
          <button :class="{ active: editMode === 'preview' }" @click="editMode = 'preview'" title="预览模式">👁️</button>
        </div>
      </div>
    </div>

    <!-- ─── Markdown 工具栏 ─── -->
    <div v-show="editMode !== 'preview'" class="md-toolbar">
      <div class="md-group">
        <button @click="toolbarActions.bold()" title="粗体 (Ctrl+B)" class="tb-btn"><b>B</b></button>
        <button @click="toolbarActions.italic()" title="斜体 (Ctrl+I)" class="tb-btn"><i>I</i></button>
        <button @click="toolbarActions.strikethrough()" title="删除线 (Ctrl+Shift+S)" class="tb-btn"><s>S</s></button>
        <button @click="toolbarActions.code()" title="行内代码" class="tb-btn mono">&lt;/&gt;</button>
      </div>
      <div class="md-sep"></div>
      <div class="md-group">
        <button @click="toolbarActions.h1()" title="一级标题" class="tb-btn">H1</button>
        <button @click="toolbarActions.h2()" title="二级标题" class="tb-btn">H2</button>
        <button @click="toolbarActions.h3()" title="三级标题" class="tb-btn">H3</button>
      </div>
      <div class="md-sep"></div>
      <div class="md-group">
        <button @click="toolbarActions.ul()" title="无序列表" class="tb-btn">• 列表</button>
        <button @click="toolbarActions.ol()" title="有序列表" class="tb-btn">1. 列表</button>
        <button @click="toolbarActions.task()" title="待办事项" class="tb-btn">☑ 待办</button>
        <button @click="toolbarActions.quote()" title="引用" class="tb-btn">❝ 引用</button>
      </div>
      <div class="md-sep"></div>
      <div class="md-group">
        <button @click="toolbarActions.link()" title="链接 (Ctrl+K)" class="tb-btn">🔗</button>
        <button @click="toolbarActions.image()" title="图片" class="tb-btn">🖼️</button>
        <button @click="toolbarActions.codeBlock()" title="代码块" class="tb-btn mono">```</button>
        <button @click="toolbarActions.table()" title="表格" class="tb-btn">⊞ 表格</button>
        <button @click="toolbarActions.hr()" title="分割线" class="tb-btn">—</button>
      </div>
      <div class="md-sep"></div>
      <div class="md-group">
        <span class="tb-hint">粘贴 HTML 自动转 Markdown</span>
      </div>
    </div>

    <!-- ─── 标签栏 ─── -->
    <div class="tags-bar">
      <div class="tags-list">
        <span
          v-for="tag in noteTags"
          :key="tag.id"
          class="tag-chip"
        >
          <span class="tag-name">#{{ tag.name }}</span>
          <button class="tag-remove" @click="removeTag(tag.name)" title="移除标签">×</button>
        </span>
        <button v-if="!showTagInput" class="tag-add-btn" @click="startAddTag" title="添加标签">+ 标签</button>
        <input
          v-else
          ref="tagInputRef"
          v-model="newTagInput"
          class="tag-input"
          placeholder="输入标签名..."
          @keydown="onTagKeydown"
          @blur="showTagInput = false"
        />
      </div>
    </div>

    <!-- ─── 内容区 ─── -->
    <div class="editor-content" :class="editMode">
      <div v-show="editMode !== 'preview'" ref="editorContainer" class="cm-container"></div>
      <div v-show="editMode !== 'edit'" ref="previewRef" class="preview-pane" @click="handlePreviewClick" @scroll="handlePreviewScroll" v-html="previewHtml"></div>
      <!-- 大纲面板 -->
      <OutlinePanel
        v-if="showOutline && editMode !== 'edit'"
        :content="noteContent"
        :active-slug="activeOutlineSlug"
        @scroll-to="handleOutlineScroll"
      />
    </div>

    <!-- 版本历史弹框 -->
    <VersionHistory
      :note-id="props.note.id"
      :visible="showVersionHistory"
      @close="showVersionHistory = false"
      @restore="handleVersionRestore"
    />
  </div>
</template>

<style scoped>
.editor-wrapper {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  background: var(--bg-primary);
}

/* ─── 标题栏 ─── */
.editor-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 16px;
  border-bottom: 1px solid var(--border);
  background: var(--bg-secondary);
  gap: 12px;
}

.toolbar-right {
  display: flex;
  align-items: center;
  gap: 6px;
}

.btn-toolbar {
  height: 32px;
  width: 32px;
  border: none;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-secondary);
  font-size: 16px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all var(--duration-fast) var(--ease-out);
}

.btn-toolbar:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

.btn-toolbar.active {
  background: var(--accent-glow);
  color: var(--accent);
}

.note-title-input {
  flex: 1;
  background: transparent;
  border: none;
  color: var(--text-primary);
  font-size: 18px;
  font-weight: 600;
  outline: none;
  padding: 4px 0;
  letter-spacing: -0.3px;
  transition: color var(--duration-fast);
}

.note-title-input::placeholder { color: var(--text-faint); }
.note-title-input:focus { color: #fff; }

/* ─── 模式切换 ─── */
.mode-switch {
  display: flex;
  gap: 2px;
  background: var(--bg-primary);
  border-radius: var(--radius-sm);
  padding: 3px;
  border: 1px solid var(--border);
}

.mode-switch button {
  background: transparent;
  border: none;
  border-radius: 4px;
  padding: 5px 10px;
  cursor: pointer;
  font-size: 12px;
  transition: all var(--duration-fast) var(--ease-out);
  opacity: 0.4;
  color: var(--text-secondary);
}

.mode-switch button:hover { opacity: 0.7; background: var(--bg-hover); }
.mode-switch button.active {
  opacity: 1;
  background: var(--bg-active);
  color: var(--text-primary);
  box-shadow: var(--shadow-sm);
}

/* ─── Markdown 工具栏 ─── */
.md-toolbar {
  display: flex;
  align-items: center;
  padding: 4px 12px;
  border-bottom: 1px solid var(--border);
  background: var(--bg-secondary);
  gap: 2px;
  overflow-x: auto;
  flex-shrink: 0;
}

.md-group {
  display: flex;
  align-items: center;
  gap: 2px;
}

.md-sep {
  width: 1px;
  height: 20px;
  background: var(--border);
  margin: 0 6px;
  flex-shrink: 0;
}

.tb-btn {
  height: 28px;
  padding: 0 8px;
  border: none;
  border-radius: 4px;
  background: transparent;
  color: var(--text-secondary);
  font-size: 12px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.1s;
  white-space: nowrap;
  font-family: inherit;
}

.tb-btn:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

.tb-btn:active {
  background: var(--bg-active);
  transform: scale(0.95);
}

.tb-btn.mono {
  font-family: 'JetBrains Mono', Consolas, monospace;
  font-size: 11px;
  letter-spacing: -0.5px;
}

.tb-hint {
  font-size: 11px;
  color: var(--text-faint);
  padding: 0 6px;
  white-space: nowrap;
  font-style: italic;
}

/* ─── Tags Bar ─── */
.tags-bar {
  display: flex;
  align-items: center;
  padding: 4px 12px;
  border-bottom: 1px solid var(--border);
  background: var(--bg-secondary);
  flex-shrink: 0;
  min-height: 32px;
}

.tags-list {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}

.tag-chip {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 8px;
  background: var(--purple-glow);
  border: 1px solid rgba(187, 154, 247, 0.2);
  border-radius: 12px;
  font-size: 11px;
  color: var(--purple);
  font-weight: 500;
  transition: all var(--duration-fast) var(--ease-out);
  white-space: nowrap;
}

.tag-chip:hover {
  background: rgba(187, 154, 247, 0.22);
  border-color: rgba(187, 154, 247, 0.35);
}

.tag-name {
  cursor: default;
}

.tag-remove {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 14px;
  height: 14px;
  border: none;
  border-radius: 50%;
  background: transparent;
  color: var(--purple);
  font-size: 12px;
  cursor: pointer;
  padding: 0;
  line-height: 1;
  transition: all 0.1s;
  opacity: 0.5;
}

.tag-remove:hover {
  opacity: 1;
  background: rgba(187, 154, 247, 0.3);
}

.tag-add-btn {
  padding: 2px 10px;
  border: 1px dashed var(--border);
  border-radius: 12px;
  background: transparent;
  color: var(--text-faint);
  font-size: 11px;
  cursor: pointer;
  transition: all var(--duration-fast) var(--ease-out);
  font-family: inherit;
  white-space: nowrap;
}

.tag-add-btn:hover {
  border-color: var(--purple);
  color: var(--purple);
  background: var(--purple-glow);
}

.tag-input {
  width: 100px;
  padding: 2px 8px;
  border: 1px solid var(--purple);
  border-radius: 12px;
  background: var(--bg-primary);
  color: var(--text-primary);
  font-size: 11px;
  outline: none;
  font-family: inherit;
}

.tag-input::placeholder {
  color: var(--text-faint);
}

.tag-input:focus {
  box-shadow: 0 0 0 2px var(--purple-glow);
}

/* ─── 内容区 ─── */
.editor-content {
  flex: 1;
  display: flex;
  overflow: hidden;
}

.editor-content.split .cm-container,
.editor-content.split .preview-pane { flex: 1; min-width: 0; }
.editor-content.edit .cm-container { flex: 1; }
.editor-content.preview .preview-pane { flex: 1; min-width: 0; }

.cm-container {
  height: 100%;
  overflow: hidden;
  border-right: 1px solid var(--border);
}

.preview-pane {
  height: 100%;
  overflow-y: auto;
  padding: 24px 32px;
  line-height: 1.75;
  color: var(--text-secondary);
}

/* ─── Preview Markdown Styles ─── */
.preview-pane :deep(h1) {
  font-size: 28px; color: var(--text-primary);
  margin: 28px 0 14px; padding-bottom: 10px;
  border-bottom: 1px solid var(--border);
  letter-spacing: -0.5px;
}
.preview-pane :deep(h2) {
  font-size: 22px; color: var(--text-primary);
  margin: 24px 0 12px; letter-spacing: -0.3px;
}
.preview-pane :deep(h3) {
  font-size: 18px; color: var(--text-primary);
  margin: 20px 0 10px;
}
.preview-pane :deep(h4) {
  font-size: 16px; color: var(--text-primary);
  margin: 16px 0 8px;
}
.preview-pane :deep(p) { margin: 10px 0; }
.preview-pane :deep(a) {
  color: var(--accent);
  text-decoration: none;
  border-bottom: 1px solid transparent;
  transition: border-color var(--duration-fast);
}
.preview-pane :deep(a:hover) { border-bottom-color: var(--accent); }
.preview-pane :deep(.wiki-link) {
  color: var(--purple);
  background: var(--purple-glow);
  padding: 2px 8px;
  border-radius: 4px;
  cursor: pointer;
  border-bottom: none;
  transition: background var(--duration-fast);
  font-weight: 500;
}
.preview-pane :deep(.wiki-link:hover) {
  background: rgba(187, 154, 247, 0.22);
  text-decoration: none;
  border-bottom: none;
}
.preview-pane :deep(code) {
  background: var(--bg-active);
  padding: 2px 7px;
  border-radius: 4px;
  font-size: 13px;
  font-family: 'JetBrains Mono', Consolas, monospace;
  color: var(--green);
}
.preview-pane :deep(pre) {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  padding: 16px;
  overflow-x: auto;
  margin: 14px 0;
}
.preview-pane :deep(pre code) {
  background: transparent;
  padding: 0;
  color: var(--text-secondary);
  font-size: 13px;
}
.preview-pane :deep(blockquote) {
  border-left: 3px solid var(--accent);
  margin: 14px 0;
  padding: 6px 18px;
  color: var(--text-muted);
  background: var(--accent-subtle);
  border-radius: 0 var(--radius-sm) var(--radius-sm) 0;
}
.preview-pane :deep(ul), .preview-pane :deep(ol) { padding-left: 24px; margin: 10px 0; }
.preview-pane :deep(li) { margin: 5px 0; }
.preview-pane :deep(li)::marker { color: var(--text-muted); }
.preview-pane :deep(hr) { border: none; border-top: 1px solid var(--border); margin: 24px 0; }
.preview-pane :deep(table) { border-collapse: collapse; width: 100%; margin: 14px 0; }
.preview-pane :deep(th), .preview-pane :deep(td) { border: 1px solid var(--border); padding: 8px 14px; text-align: left; }
.preview-pane :deep(th) { background: var(--bg-secondary); color: var(--text-primary); font-weight: 600; }
.preview-pane :deep(img) { max-width: 100%; border-radius: var(--radius-md); box-shadow: var(--shadow-md); }
.preview-pane :deep(.empty-preview) { color: var(--text-faint); font-style: italic; text-align: center; padding: 48px 0; }

/* ─── Outline highlight animation ─── */
.preview-pane :deep(.outline-highlight) {
  animation: outlineFlash 1.5s ease-out;
  border-radius: 4px;
}

@keyframes outlineFlash {
  0% { background: rgba(122, 162, 247, 0.3); }
  100% { background: transparent; }
}
</style>
