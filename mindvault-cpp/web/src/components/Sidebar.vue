<script setup lang="ts">
import { ref, computed, nextTick } from 'vue'
import { notesApi, tagsApi, type Note, type Tag } from '../api'
import FolderTree, { type FolderNode } from './FolderTree.vue'

const props = defineProps<{
  notes: Note[]
  selectedId?: number
  searchQuery: string
  searchResults: Note[]
  isSearching: boolean
  isLoadingTag?: boolean
  activeTag?: string | null
}>()

const emit = defineEmits<{
  select: [note: Note]
  create: [title: string, folder: string]
  delete: [id: number]
  rename: [id: number, newTitle: string]
  move: [id: number, targetFolder: string]
  search: [query: string]
  createFolder: [folderPath: string]
  refresh: []
  filterTag: [tagName: string | null]
}>()

// ─── 文件夹树 ───

const collapsedFolders = ref<Set<string>>(new Set())

// 从笔记列表构建文件夹树
const folderTree = computed(() => {
  const root: FolderNode = {
    name: '全部笔记',
    path: '',
    expanded: true,
    children: [],
    notes: [],
  }

  // 收集所有文件夹路径
  const folderSet = new Set<string>()
  for (const note of props.notes) {
    const folder = note.folder || 'default'
    folderSet.add(folder)
    // 添加所有父级路径
    const parts = folder.split('/')
    for (let i = 1; i <= parts.length; i++) {
      folderSet.add(parts.slice(0, i).join('/'))
    }
  }

  // 按 path 排序构建树
  const sorted = [...folderSet].sort()
  const nodeMap = new Map<string, FolderNode>()

  for (const path of sorted) {
    const parts = path.split('/')
    const name = parts[parts.length - 1]
    const node: FolderNode = {
      name,
      path,
      expanded: !collapsedFolders.value.has(path),
      children: [],
      notes: [],
    }
    nodeMap.set(path, node)
  }

  // 挂载子节点
  for (const path of sorted) {
    const node = nodeMap.get(path)!
    const parts = path.split('/')
    if (parts.length <= 1) {
      root.children.push(node)
    } else {
      const parentPath = parts.slice(0, -1).join('/')
      const parent = nodeMap.get(parentPath)
      if (parent) parent.children.push(node)
      else root.children.push(node)
    }
  }

  // 分配笔记到对应文件夹
  for (const note of props.notes) {
    const folder = note.folder || 'default'
    const node = nodeMap.get(folder)
    if (node) node.notes.push(note)
    else root.notes.push(note)
  }

  return root
})

function toggleFolder(path: string) {
  if (collapsedFolders.value.has(path)) {
    collapsedFolders.value.delete(path)
  } else {
    collapsedFolders.value.add(path)
  }
  collapsedFolders.value = new Set(collapsedFolders.value)
}

// ─── 右键菜单 ───

interface ContextMenuItem {
  label: string
  icon: string
  action: () => void
  danger?: boolean
}

const contextMenu = ref<{
  visible: boolean
  x: number
  y: number
  items: ContextMenuItem[]
}>({
  visible: false,
  x: 0,
  y: 0,
  items: [],
})

let contextTarget = ref<{ type: 'note' | 'folder'; id?: number; path?: string }>({
  type: 'note',
})

function showNoteContext(e: MouseEvent, note: Note) {
  e.preventDefault()
  contextTarget.value = { type: 'note', id: note.id, path: note.folder }
  contextMenu.value = {
    visible: true,
    x: e.clientX,
    y: e.clientY,
    items: [
      { label: '重命名', icon: '✏️', action: startRename },
      { label: '移动到...', icon: '📁', action: showMoveDialog },
      { label: '复制标题', icon: '📋', action: () => copyTitle(note) },
      { label: '删除', icon: '🗑️', action: confirmDelete, danger: true },
    ],
  }
}

function showFolderContext(e: MouseEvent, path: string) {
  e.preventDefault()
  contextTarget.value = { type: 'folder', path }
  contextMenu.value = {
    visible: true,
    x: e.clientX,
    y: e.clientY,
    items: [
      { label: '新建笔记', icon: '📝', action: () => showCreateInFolder(path) },
      { label: '新建子文件夹', icon: '📂', action: () => showNewSubfolder(path) },
    ],
  }
}

function hideContextMenu() {
  contextMenu.value.visible = false
}

// ─── 删除确认 ───

const deleteConfirm = ref<{ visible: boolean; noteId: number; noteTitle: string }>({
  visible: false,
  noteId: 0,
  noteTitle: '',
})

function confirmDelete() {
  hideContextMenu()
  const id = contextTarget.value.id!
  const note = props.notes.find(n => n.id === id)
  deleteConfirm.value = {
    visible: true,
    noteId: id,
    noteTitle: note?.title || '无标题',
  }
}

function doDelete() {
  emit('delete', deleteConfirm.value.noteId)
  deleteConfirm.value.visible = false
  // 删除后自动刷新回收站
  if (trashExpanded.value) {
    setTimeout(() => loadTrash(), 300)
  }
}

// ─── 重命名 ───

const renameState = ref<{ visible: boolean; noteId: number; oldTitle: string; newTitle: string }>({
  visible: false,
  noteId: 0,
  oldTitle: '',
  newTitle: '',
})

function startRename() {
  hideContextMenu()
  const id = contextTarget.value.id!
  const note = props.notes.find(n => n.id === id)
  renameState.value = {
    visible: true,
    noteId: id,
    oldTitle: note?.title || '',
    newTitle: note?.title || '',
  }
  nextTick(() => {
    const input = document.querySelector('.rename-input') as HTMLInputElement
    input?.focus()
    input?.select()
  })
}

function doRename() {
  if (renameState.value.newTitle.trim() && renameState.value.newTitle !== renameState.value.oldTitle) {
    emit('rename', renameState.value.noteId, renameState.value.newTitle.trim())
  }
  renameState.value.visible = false
}

// ─── 移动笔记 ───

const moveDialog = ref<{ visible: boolean; noteId: number; targetFolder: string }>({
  visible: false,
  noteId: 0,
  targetFolder: 'default',
})

function showMoveDialog() {
  hideContextMenu()
  moveDialog.value = {
    visible: true,
    noteId: contextTarget.value.id!,
    targetFolder: contextTarget.value.path || 'default',
  }
}

function doMove() {
  emit('move', moveDialog.value.noteId, moveDialog.value.targetFolder)
  moveDialog.value.visible = false
}

// ─── 创建笔记弹框 ───

const createDialog = ref<{ visible: boolean; title: string; folder: string }>({
  visible: false,
  title: '',
  folder: 'default',
})

function showCreateDialog() {
  createDialog.value = {
    visible: true,
    title: '',
    folder: 'default',
  }
  nextTick(() => {
    const input = document.querySelector('.create-title-input') as HTMLInputElement
    input?.focus()
  })
}

function showCreateInFolder(folder: string) {
  hideContextMenu()
  createDialog.value = {
    visible: true,
    title: '',
    folder,
  }
  nextTick(() => {
    const input = document.querySelector('.create-title-input') as HTMLInputElement
    input?.focus()
  })
}

function doCreate() {
  const title = createDialog.value.title.trim()
  if (!title) return
  emit('create', title, createDialog.value.folder)
  createDialog.value.visible = false
}

// ─── 新建子文件夹 ───

const newSubfolderState = ref<{ visible: boolean; parentPath: string; name: string }>({
  visible: false,
  parentPath: '',
  name: '',
})

function showNewSubfolder(parentPath: string) {
  hideContextMenu()
  newSubfolderState.value = {
    visible: true,
    parentPath,
    name: '',
  }
  nextTick(() => {
    const input = document.querySelector('.new-folder-input') as HTMLInputElement
    input?.focus()
  })
}

function doCreateSubfolder() {
  const name = newSubfolderState.value.name.trim()
  if (!name) return
  const path = newSubfolderState.value.parentPath
    ? `${newSubfolderState.value.parentPath}/${name}`
    : name
  emit('createFolder', path)
  newSubfolderState.value.visible = false
}

// ─── 新建根文件夹 ───

const newRootFolderState = ref<{ visible: boolean; name: string }>({
  visible: false,
  name: '',
})

function showNewRootFolder() {
  newRootFolderState.value = { visible: true, name: '' }
  nextTick(() => {
    const input = document.querySelector('.new-root-folder-input') as HTMLInputElement
    input?.focus()
  })
}

function doCreateRootFolder() {
  const name = newRootFolderState.value.name.trim()
  if (!name) return
  emit('createFolder', name)
  newRootFolderState.value.visible = false
}

// ─── 工具函数 ───

function copyTitle(note: Note) {
  hideContextMenu()
  // 复制为 Wiki 链接格式 [[标题]]
  const wikiLink = `[[${note.title}]]`

  // 使用 navigator.clipboard API（如果可用）
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(wikiLink).then(() => {
      console.log('Copied:', wikiLink)
    }).catch(() => {
      // 降级方案
      fallbackCopy(wikiLink)
    })
  } else {
    // 降级方案
    fallbackCopy(wikiLink)
  }
}

function fallbackCopy(text: string) {
  try {
    const textarea = document.createElement('textarea')
    textarea.value = text
    textarea.style.position = 'fixed'
    textarea.style.left = '-9999px'
    textarea.style.top = '-9999px'
    textarea.style.opacity = '0'
    document.body.appendChild(textarea)
    textarea.focus()
    textarea.select()
    const result = document.execCommand('copy')
    document.body.removeChild(textarea)
    console.log('Copied (fallback):', text, 'Result:', result)
  } catch (e) {
    console.error('Copy failed:', e)
  }
}

function formatDate(dateStr: string) {
  if (!dateStr) return ''
  const d = new Date(dateStr)
  const now = new Date()
  const diff = now.getTime() - d.getTime()
  const mins = Math.floor(diff / 60000)
  if (mins < 1) return '刚刚'
  if (mins < 60) return `${mins}分钟前`
  const hours = Math.floor(mins / 60)
  if (hours < 24) return `${hours}小时前`
  return d.toLocaleDateString()
}

function getFolderIcon(path: string): string {
  const name = path.split('/').pop()?.toLowerCase() || ''
  if (name.includes('学习') || name.includes('study')) return '📚'
  if (name.includes('工作') || name.includes('work')) return '💼'
  if (name.includes('项目') || name.includes('project')) return '🚀'
  if (name.includes('日记') || name.includes('diary')) return '📔'
  if (name.includes('想法') || name.includes('idea')) return '💡'
  if (name.includes('收藏') || name.includes('fav')) return '⭐'
  return '📁'
}

// 所有文件夹路径（给移动对话框用）
const allFolders = computed(() => {
  const folders = new Set<string>()
  for (const note of props.notes) {
    folders.add(note.folder || 'default')
    const parts = (note.folder || 'default').split('/')
    for (let i = 1; i <= parts.length; i++) {
      folders.add(parts.slice(0, i).join('/'))
    }
  }
  return [...folders].sort()
})

// 点击空白关闭右键菜单
function onGlobalClick() {
  hideContextMenu()
}

// 侧边栏头部右键
function showHeaderContext(e: MouseEvent) {
  e.preventDefault()
  contextMenu.value = {
    visible: true,
    x: e.clientX,
    y: e.clientY,
    items: [
      { label: '新建笔记', icon: '📝', action: showCreateDialog },
      { label: '新建文件夹', icon: '📂', action: showNewRootFolder },
    ],
  }
}

// FolderTree 事件中转
function onNoteContextFromTree(e: MouseEvent, note: Note) {
  showNoteContext(e, note)
}

function onFolderContextFromTree(e: MouseEvent, path: string) {
  showFolderContext(e, path)
}

// ─── 拖拽排序 ───

const dragTargetId = ref<number | null>(null)
const dragOverId = ref<number | null>(null)

function onDragStart(e: DragEvent, note: Note) {
  if (!e.dataTransfer) return
  dragTargetId.value = note.id
  e.dataTransfer.effectAllowed = 'move'
  e.dataTransfer.setData('text/plain', note.id.toString())
}

function onDragOver(_e: DragEvent, note: Note) {
  if (dragTargetId.value === note.id) return
  dragOverId.value = note.id
}

function onDragLeave() {
  dragOverId.value = null
}

// 拖拽到文件夹
async function onFolderDrop(e: DragEvent, folderPath: string) {
  e.preventDefault()
  if (!dragTargetId.value) return

  const sourceId = dragTargetId.value
  dragTargetId.value = null

  // 获取源笔记
  const sourceNote = props.notes.find(n => n.id === sourceId)
  if (!sourceNote) return

  // 如果目标文件夹和源文件夹不同，改变文件夹
  const sourceFolder = sourceNote.folder || 'default'
  if (sourceFolder !== folderPath) {
    try {
      await notesApi.update(sourceId, {
        title: sourceNote.title,
        content: sourceNote.content || '',
        folder: folderPath
      })
      emit('refresh')
    } catch (err) {
      console.error('Failed to move note:', err)
    }
  }
}

async function onDrop(e: DragEvent, targetNote: Note) {
  e.preventDefault()
  dragOverId.value = null

  if (!dragTargetId.value || dragTargetId.value === targetNote.id) {
    dragTargetId.value = null
    return
  }

  const sourceId = dragTargetId.value
  dragTargetId.value = null

  // 获取源笔记
  const sourceNote = props.notes.find(n => n.id === sourceId)
  if (!sourceNote) return

  const targetFolder = targetNote.folder || 'default'
  const sourceFolder = sourceNote.folder || 'default'

  // 如果源笔记和目标笔记不在同一个文件夹，改变源笔记的文件夹
  if (sourceFolder !== targetFolder) {
    try {
      await notesApi.update(sourceId, {
        title: sourceNote.title,
        content: sourceNote.content || '',
        folder: targetFolder
      })
      emit('refresh')
    } catch (err) {
      console.error('Failed to move note:', err)
    }
    return
  }

  // 同一个文件夹内排序
  const notesInFolder = props.notes.filter(n => (n.folder || 'default') === targetFolder)
  const sourceIdx = notesInFolder.findIndex(n => n.id === sourceId)
  const targetIdx = notesInFolder.findIndex(n => n.id === targetNote.id)

  if (sourceIdx === -1 || targetIdx === -1) return

  // 重新排列
  const [moved] = notesInFolder.splice(sourceIdx, 1)
  notesInFolder.splice(targetIdx, 0, moved)

  // 更新排序值
  const orders = notesInFolder.map((n, i) => ({ id: n.id, order: i }))
  try {
    await notesApi.batchUpdateOrder(orders)
    emit('refresh')
  } catch (err) {
    console.error('Failed to update sort order:', err)
  }
}

// ─── 标签管理 ───

const tagsExpanded = ref(false)
const allTags = ref<Tag[]>([])
const tagsLoaded = ref(false)

async function toggleTags() {
  tagsExpanded.value = !tagsExpanded.value
  if (tagsExpanded.value && !tagsLoaded.value) {
    await loadTags()
  }
}

async function loadTags() {
  try {
    allTags.value = await tagsApi.list()
    tagsLoaded.value = true
  } catch (e) {
    console.error('Failed to load tags:', e)
  }
}

function clickTag(tagName: string) {
  if (props.activeTag === tagName) {
    emit('filterTag', null)
  } else {
    emit('filterTag', tagName)
  }
}

function clearTagFilter() {
  emit('filterTag', null)
}

// ─── 回收站 ───

const trashExpanded = ref(false)
const trashNotes = ref<Note[]>([])
const trashLoaded = ref(false)

async function toggleTrash() {
  trashExpanded.value = !trashExpanded.value
  if (trashExpanded.value && !trashLoaded.value) {
    await loadTrash()
  }
}

async function loadTrash() {
  try {
    trashNotes.value = await notesApi.trash()
    trashLoaded.value = true
  } catch (e) {
    console.error('Failed to load trash:', e)
  }
}

async function restoreNote(id: number) {
  try {
    await notesApi.restore(id)
    trashNotes.value = trashNotes.value.filter(n => n.id !== id)
    emit('refresh')
  } catch (e) {
    console.error('Failed to restore note:', e)
  }
}

const permanentDeleteConfirm = ref<{ visible: boolean; noteId: number; noteTitle: string }>({
  visible: false,
  noteId: 0,
  noteTitle: '',
})

function confirmPermanentDelete(note: Note) {
  permanentDeleteConfirm.value = {
    visible: true,
    noteId: note.id,
    noteTitle: note.title || '无标题',
  }
}

async function doPermanentDelete() {
  try {
    await notesApi.permanentDelete(permanentDeleteConfirm.value.noteId)
    trashNotes.value = trashNotes.value.filter(n => n.id !== permanentDeleteConfirm.value.noteId)
  } catch (e) {
    console.error('Failed to permanently delete:', e)
  }
  permanentDeleteConfirm.value.visible = false
}

const emptyTrashConfirm = ref(false)

async function doEmptyTrash() {
  try {
    await notesApi.emptyTrash()
    trashNotes.value = []
    emptyTrashConfirm.value = false
  } catch (e) {
    console.error('Failed to empty trash:', e)
  }
}
</script>

<template>
  <aside class="sidebar" @click="onGlobalClick">
    <!-- Header -->
    <div class="sidebar-header" @contextmenu.prevent="showHeaderContext($event)">
      <h1 class="logo">
        <span class="logo-icon">🧠</span> MindVault
      </h1>
      <div class="header-actions">
        <button class="btn-icon" @click="showNewRootFolder" title="新建文件夹">
          📁+
        </button>
        <button class="btn-icon btn-new" @click="showCreateDialog" title="新建笔记">
          📝
        </button>
      </div>
    </div>

    <!-- Search -->
    <div class="search-box">
      <span class="search-icon">🔍</span>
      <input
        type="text"
        :value="searchQuery"
        @input="emit('search', ($event.target as HTMLInputElement).value)"
        placeholder="搜索笔记..."
        class="search-input"
      />
    </div>

    <!-- Folder Tree + Note List -->
    <div class="tree-container">
      <!-- 标签筛选提示 -->
      <div v-if="props.activeTag" class="tag-filter-banner">
        <span class="filter-icon">🏷️</span>
        <span class="filter-text">#{{ props.activeTag }}</span>
        <button class="filter-clear" @click="clearTagFilter" title="清除筛选">×</button>
      </div>
      <div v-if="props.isLoadingTag" class="tag-loading">加载中...</div>

      <template v-if="isSearching">
        <div class="section-label">搜索结果</div>
        <div v-if="searchResults.length === 0" class="empty-hint">
          未找到结果
        </div>
        <div
          v-for="note in searchResults"
          :key="note.id"
          class="note-item"
          :class="{ active: note.id === selectedId }"
          @click="emit('select', note)"
        >
          <span class="note-icon">📄</span>
          <div class="note-info">
            <div class="note-title">{{ note.title || '无标题' }}</div>
            <div class="note-meta">{{ formatDate(note.updated_at) }}</div>
          </div>
        </div>
      </template>
      <template v-else>
        <!-- 递归渲染文件夹树 -->
        <div v-for="folder in folderTree.children" :key="folder.path">
          <div
            class="folder-item"
            @click="toggleFolder(folder.path)"
            @contextmenu.prevent="showFolderContext($event, folder.path)"
          >
            <span
              class="folder-toggle"
              @click.stop="toggleFolder(folder.path)"
            >
              {{ collapsedFolders.has(folder.path) ? '▶' : '▼' }}
            </span>
            <span class="folder-icon">{{ getFolderIcon(folder.path) }}</span>
            <span class="folder-name">{{ folder.name }}</span>
            <span class="folder-count">{{ folder.notes.length }}</span>
          </div>
          <!-- 子内容 -->
          <div v-show="!collapsedFolders.has(folder.path)" class="folder-content">
            <!-- 子文件夹递归 -->
            <FolderTree
              v-for="child in folder.children"
              :key="child.path"
              :folder="child"
              :selected-id="selectedId"
              :collapsed="collapsedFolders"
              :active-folder="''"
              :depth="1"
              @select-note="(n: Note | null) => n && emit('select', n)"
              @toggle-folder="toggleFolder"
              @folder-context="onFolderContextFromTree"
              @note-context="onNoteContextFromTree"
              @note-drop="onDrop"
              @note-drag-start="onDragStart"
              @folder-drop="onFolderDrop"
            />
            <!-- 当前文件夹的笔记 -->
            <div
              v-for="note in folder.notes"
              :key="note.id"
              class="note-item"
              :class="{
                active: note.id === selectedId,
                'drag-over': dragTargetId === note.id && dragOverId === note.id
              }"
              draggable="true"
              @click="emit('select', note)"
              @contextmenu.prevent="showNoteContext($event, note)"
              @dragstart="onDragStart($event, note)"
              @dragover.prevent="onDragOver($event, note)"
              @dragleave="onDragLeave"
              @drop.prevent="onDrop($event, note)"
            >
              <span class="note-icon">📄</span>
              <div class="note-info">
                <div class="note-title">{{ note.title || '无标题' }}</div>
                <div class="note-meta">{{ formatDate(note.updated_at) }}</div>
              </div>
            </div>
          </div>
        </div>
        <!-- 没有文件夹的根级笔记 -->
        <div
          v-for="note in folderTree.notes"
          :key="'root-' + note.id"
          class="note-item"
          :class="{
            active: note.id === selectedId,
            'drag-over': dragTargetId === note.id && dragOverId === note.id
          }"
          draggable="true"
          @click="emit('select', note)"
          @contextmenu.prevent="showNoteContext($event, note)"
          @dragstart="onDragStart($event, note)"
          @dragover.prevent="onDragOver($event, note)"
          @dragleave="onDragLeave"
          @drop.prevent="onDrop($event, note)"
        >
          <span class="note-icon">📄</span>
          <div class="note-info">
            <div class="note-title">{{ note.title || '无标题' }}</div>
            <div class="note-meta">{{ formatDate(note.updated_at) }}</div>
          </div>
        </div>
      </template>
    </div>

    <!-- 标签管理 -->
    <div class="tags-section">
      <div class="tags-header" @click="toggleTags">
        <span class="folder-toggle">{{ tagsExpanded ? '▼' : '▶' }}</span>
        <span class="tags-icon">🏷️</span>
        <span class="tags-label">标签</span>
        <span v-if="allTags.length > 0" class="tags-count">{{ allTags.length }}</span>
      </div>
      <div v-show="tagsExpanded" class="tags-content">
        <div v-if="!tagsLoaded" class="tags-loading">点击加载...</div>
        <div v-else-if="allTags.length === 0" class="tags-empty">暂无标签</div>
        <template v-else>
          <button v-if="props.activeTag" class="tags-clear-btn" @click="clearTagFilter">
            清除筛选
          </button>
          <div class="tags-cloud">
            <button
              v-for="tag in allTags.filter(t => t.note_count && t.note_count > 0)"
              :key="tag.id"
              class="tag-item"
              :class="{ active: props.activeTag === tag.name }"
              @click="clickTag(tag.name)"
            >
              <span class="tag-name">#{{ tag.name }}</span>
              <span class="tag-count">{{ tag.note_count }}</span>
            </button>
          </div>
        </template>
      </div>
    </div>

    <!-- 回收站 -->
    <div class="trash-section">
      <div class="trash-header" @click="toggleTrash">
        <span class="folder-toggle">{{ trashExpanded ? '▼' : '▶' }}</span>
        <span class="trash-icon">🗑️</span>
        <span class="trash-label">回收站</span>
        <span v-if="trashNotes.length > 0" class="trash-count">{{ trashNotes.length }}</span>
      </div>
      <div v-show="trashExpanded" class="trash-content">
        <div v-if="!trashLoaded" class="trash-loading">点击加载...</div>
        <div v-else-if="trashNotes.length === 0" class="trash-empty">回收站为空</div>
        <template v-else>
          <button class="trash-action-btn empty-all" @click="emptyTrashConfirm = true">
            清空回收站
          </button>
          <div
            v-for="note in trashNotes"
            :key="'trash-' + note.id"
            class="trash-item"
          >
            <div class="trash-item-info">
              <div class="trash-item-title">{{ note.title || '无标题' }}</div>
              <div class="trash-item-meta">{{ formatDate(note.updated_at) }} · {{ note.folder }}</div>
            </div>
            <div class="trash-item-actions">
              <button class="trash-btn restore" @click="restoreNote(note.id)" title="恢复">↩️</button>
              <button class="trash-btn delete-perm" @click="confirmPermanentDelete(note)" title="永久删除">❌</button>
            </div>
          </div>
        </template>
      </div>
    </div>

    <!-- 右键菜单 -->
    <Teleport to="body">
      <div
        v-if="contextMenu.visible"
        class="context-menu"
        :style="{ left: contextMenu.x + 'px', top: contextMenu.y + 'px' }"
        @click.stop
      >
        <button
          v-for="(item, i) in contextMenu.items"
          :key="i"
          class="ctx-item"
          :class="{ danger: item.danger }"
          @click="item.action()"
        >
          <span class="ctx-icon">{{ item.icon }}</span>
          {{ item.label }}
        </button>
      </div>
    </Teleport>

    <!-- 删除确认弹框 -->
    <Teleport to="body">
      <div v-if="deleteConfirm.visible" class="modal-overlay" @click.self="deleteConfirm.visible = false">
        <div class="modal-box">
          <div class="modal-title">⚠️ 确认删除</div>
          <div class="modal-body">
            确定要删除笔记「<strong>{{ deleteConfirm.noteTitle }}</strong>」吗？笔记将移入回收站，可随时恢复。
          </div>
          <div class="modal-actions">
            <button class="btn-cancel" @click="deleteConfirm.visible = false">取消</button>
            <button class="btn-danger" @click="doDelete">删除</button>
          </div>
        </div>
      </div>
    </Teleport>

    <!-- 重命名弹框 -->
    <Teleport to="body">
      <div v-if="renameState.visible" class="modal-overlay" @click.self="renameState.visible = false">
        <div class="modal-box">
          <div class="modal-title">✏️ 重命名</div>
          <input
            v-model="renameState.newTitle"
            class="modal-input rename-input"
            placeholder="输入新标题..."
            @keyup.enter="doRename"
            @keyup.escape="renameState.visible = false"
          />
          <div class="modal-actions">
            <button class="btn-cancel" @click="renameState.visible = false">取消</button>
            <button class="btn-primary" @click="doRename">确定</button>
          </div>
        </div>
      </div>
    </Teleport>

    <!-- 创建笔记弹框 -->
    <Teleport to="body">
      <div v-if="createDialog.visible" class="modal-overlay" @click.self="createDialog.visible = false">
        <div class="modal-box">
          <div class="modal-title">📝 新建笔记</div>
          <div class="modal-field">
            <label>标题</label>
            <input
              v-model="createDialog.title"
              class="modal-input create-title-input"
              placeholder="输入笔记标题..."
              @keyup.enter="doCreate"
              @keyup.escape="createDialog.visible = false"
            />
          </div>
          <div class="modal-field">
            <label>存放位置</label>
            <select v-model="createDialog.folder" class="modal-select">
              <option value="default">默认</option>
              <option v-for="f in allFolders" :key="f" :value="f">{{ f }}</option>
            </select>
          </div>
          <div class="modal-actions">
            <button class="btn-cancel" @click="createDialog.visible = false">取消</button>
            <button class="btn-primary" @click="doCreate" :disabled="!createDialog.title.trim()">创建</button>
          </div>
        </div>
      </div>
    </Teleport>

    <!-- 移动笔记弹框 -->
    <Teleport to="body">
      <div v-if="moveDialog.visible" class="modal-overlay" @click.self="moveDialog.visible = false">
        <div class="modal-box">
          <div class="modal-title">📁 移动到文件夹</div>
          <select v-model="moveDialog.targetFolder" class="modal-select">
            <option v-for="f in allFolders" :key="f" :value="f">{{ f }}</option>
          </select>
          <div class="modal-actions">
            <button class="btn-cancel" @click="moveDialog.visible = false">取消</button>
            <button class="btn-primary" @click="doMove">移动</button>
          </div>
        </div>
      </div>
    </Teleport>

    <!-- 新建子文件夹弹框 -->
    <Teleport to="body">
      <div v-if="newSubfolderState.visible" class="modal-overlay" @click.self="newSubfolderState.visible = false">
        <div class="modal-box">
          <div class="modal-title">📂 新建子文件夹</div>
          <div class="modal-body" v-if="newSubfolderState.parentPath">
            父文件夹：{{ newSubfolderState.parentPath }}
          </div>
          <input
            v-model="newSubfolderState.name"
            class="modal-input new-folder-input"
            placeholder="文件夹名称..."
            @keyup.enter="doCreateSubfolder"
            @keyup.escape="newSubfolderState.visible = false"
          />
          <div class="modal-actions">
            <button class="btn-cancel" @click="newSubfolderState.visible = false">取消</button>
            <button class="btn-primary" @click="doCreateSubfolder" :disabled="!newSubfolderState.name.trim()">创建</button>
          </div>
        </div>
      </div>
    </Teleport>

    <!-- 新建根文件夹弹框 -->
    <Teleport to="body">
      <div v-if="newRootFolderState.visible" class="modal-overlay" @click.self="newRootFolderState.visible = false">
        <div class="modal-box">
          <div class="modal-title">📂 新建文件夹</div>
          <input
            v-model="newRootFolderState.name"
            class="modal-input new-root-folder-input"
            placeholder="文件夹名称..."
            @keyup.enter="doCreateRootFolder"
            @keyup.escape="newRootFolderState.visible = false"
          />
          <div class="modal-actions">
            <button class="btn-cancel" @click="newRootFolderState.visible = false">取消</button>
            <button class="btn-primary" @click="doCreateRootFolder" :disabled="!newRootFolderState.name.trim()">创建</button>
          </div>
        </div>
      </div>
    </Teleport>

    <!-- 永久删除确认弹框 -->
    <Teleport to="body">
      <div v-if="permanentDeleteConfirm.visible" class="modal-overlay" @click.self="permanentDeleteConfirm.visible = false">
        <div class="modal-box">
          <div class="modal-title">⚠️ 永久删除</div>
          <div class="modal-body">
            确定要永久删除笔记「<strong>{{ permanentDeleteConfirm.noteTitle }}</strong>」吗？<br/>
            此操作<strong>不可恢复</strong>，笔记将被彻底删除。
          </div>
          <div class="modal-actions">
            <button class="btn-cancel" @click="permanentDeleteConfirm.visible = false">取消</button>
            <button class="btn-danger" @click="doPermanentDelete">永久删除</button>
          </div>
        </div>
      </div>
    </Teleport>

    <!-- 清空回收站确认弹框 -->
    <Teleport to="body">
      <div v-if="emptyTrashConfirm" class="modal-overlay" @click.self="emptyTrashConfirm = false">
        <div class="modal-box">
          <div class="modal-title">⚠️ 清空回收站</div>
          <div class="modal-body">
            确定要清空回收站吗？所有 <strong>{{ trashNotes.length }}</strong> 条笔记将被永久删除，<strong>不可恢复</strong>。
          </div>
          <div class="modal-actions">
            <button class="btn-cancel" @click="emptyTrashConfirm = false">取消</button>
            <button class="btn-danger" @click="doEmptyTrash">清空</button>
          </div>
        </div>
      </div>
    </Teleport>
  </aside>
</template>

<style scoped>
.sidebar {
  width: 100%;
  min-width: 0;
  background: var(--bg-secondary);
  border-right: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  overflow: hidden;
  user-select: none;
  position: relative;
}

/* ─── Header ─── */
.sidebar-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 16px;
  border-bottom: 1px solid var(--border);
  background: linear-gradient(180deg, rgba(122, 162, 247, 0.04) 0%, transparent 100%);
}

.logo {
  font-size: 16px;
  font-weight: 700;
  color: var(--accent);
  margin: 0;
  display: flex;
  align-items: center;
  gap: 6px;
  letter-spacing: -0.3px;
}

.logo-icon {
  font-size: 20px;
  filter: drop-shadow(0 2px 4px rgba(122, 162, 247, 0.3));
}

.header-actions {
  display: flex;
  gap: 4px;
}

.btn-icon {
  height: 32px;
  padding: 0 10px;
  border: none;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-secondary);
  font-size: 14px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all var(--duration-fast) var(--ease-out);
}

.btn-icon:hover {
  background: var(--bg-active);
  color: var(--text-primary);
}

.btn-new {
  background: var(--accent-glow);
  color: var(--accent);
}

.btn-new:hover {
  background: rgba(122, 162, 247, 0.22);
  box-shadow: 0 0 12px rgba(122, 162, 247, 0.1);
}

/* ─── Search ─── */
.search-box {
  padding: 10px 12px;
  position: relative;
}

.search-icon {
  position: absolute;
  left: 22px;
  top: 50%;
  transform: translateY(-50%);
  font-size: 12px;
  pointer-events: none;
  opacity: 0.5;
}

.search-input {
  width: 100%;
  padding: 8px 12px 8px 32px;
  background: var(--bg-primary);
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  color: var(--text-primary);
  font-size: 13px;
  outline: none;
  box-sizing: border-box;
  transition: border-color var(--duration-fast), box-shadow var(--duration-fast);
}

.search-input:focus {
  border-color: var(--accent);
  box-shadow: 0 0 0 3px var(--accent-glow);
}

.search-input::placeholder {
  color: var(--text-faint);
}

/* ─── Tree Container ─── */
.tree-container {
  flex: 1;
  overflow-y: auto;
  padding: 4px 0;
}

.section-label {
  padding: 10px 16px 6px;
  font-size: 11px;
  font-weight: 600;
  color: var(--text-faint);
  text-transform: uppercase;
  letter-spacing: 0.8px;
}

/* ─── Folder Item ─── */
.folder-item {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 5px 12px;
  cursor: pointer;
  border-radius: var(--radius-sm);
  margin: 1px 6px;
  transition: background var(--duration-fast) var(--ease-out);
}

.folder-item:hover {
  background: var(--bg-hover);
}

.folder-item.active {
  background: var(--accent-glow);
}

.folder-toggle {
  font-size: 8px;
  color: var(--text-muted);
  width: 14px;
  text-align: center;
  transition: transform var(--duration-normal) var(--ease-out);
}

.folder-icon {
  font-size: 14px;
  flex-shrink: 0;
}

.folder-name {
  flex: 1;
  font-size: 13px;
  font-weight: 500;
  color: var(--text-secondary);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.folder-count {
  font-size: 10px;
  color: var(--text-faint);
  background: var(--bg-primary);
  padding: 1px 6px;
  border-radius: 10px;
  min-width: 20px;
  text-align: center;
  font-weight: 500;
}

/* ─── Note Item ─── */
.note-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 7px 12px 7px 20px;
  cursor: pointer;
  border-radius: var(--radius-sm);
  margin: 1px 6px;
  transition: background var(--duration-fast) var(--ease-out);
  position: relative;
}

.note-item::before {
  content: '';
  position: absolute;
  left: 6px;
  top: 50%;
  transform: translateY(-50%);
  width: 3px;
  height: 0;
  border-radius: 2px;
  background: var(--accent);
  transition: height var(--duration-normal) var(--ease-out);
}

.note-item:hover {
  background: var(--bg-hover);
}

.note-item:hover::before {
  height: 12px;
  background: var(--text-muted);
}

.note-item.active {
  background: var(--bg-active);
}

.note-item.active::before {
  height: 18px;
  background: var(--accent);
  box-shadow: 0 0 6px rgba(122, 162, 247, 0.3);
}

.note-item.drag-over {
  border-top: 2px solid var(--accent);
  background: var(--accent-glow);
}

.note-item[draggable="true"] {
  cursor: grab;
}

.note-item[draggable="true"]:active {
  cursor: grabbing;
  opacity: 0.6;
}

.note-icon {
  font-size: 12px;
  flex-shrink: 0;
  opacity: 0.6;
}

.note-item.active .note-icon {
  opacity: 1;
}

.note-info {
  flex: 1;
  min-width: 0;
}

.note-title {
  font-size: 13px;
  font-weight: 500;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  color: var(--text-secondary);
}

.note-item.active .note-title {
  color: var(--text-primary);
}

.note-meta {
  font-size: 10px;
  color: var(--text-faint);
  margin-top: 2px;
}

.empty-hint {
  text-align: center;
  color: var(--text-faint);
  font-size: 13px;
  padding: 32px 16px;
}

/* ─── Tag Filter Banner ─── */
.tag-filter-banner {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 12px;
  margin: 4px 6px;
  background: var(--purple-glow);
  border: 1px solid rgba(187, 154, 247, 0.2);
  border-radius: var(--radius-sm);
  font-size: 12px;
}

.filter-icon {
  font-size: 12px;
}

.filter-text {
  flex: 1;
  color: var(--purple);
  font-weight: 500;
}

.filter-clear {
  width: 18px;
  height: 18px;
  border: none;
  border-radius: 50%;
  background: transparent;
  color: var(--purple);
  font-size: 14px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all var(--duration-fast);
  padding: 0;
  line-height: 1;
}

.filter-clear:hover {
  background: rgba(187, 154, 247, 0.2);
}

.tag-loading {
  text-align: center;
  color: var(--text-faint);
  font-size: 12px;
  padding: 12px 8px;
}

/* ─── Tags Section ─── */
.tags-section {
  border-top: 1px solid var(--border);
  padding: 4px 0;
  flex-shrink: 0;
}

.tags-header {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px 12px;
  cursor: pointer;
  border-radius: var(--radius-sm);
  margin: 2px 6px;
  transition: background var(--duration-fast) var(--ease-out);
}

.tags-header:hover {
  background: var(--bg-hover);
}

.tags-icon {
  font-size: 14px;
  flex-shrink: 0;
}

.tags-label {
  flex: 1;
  font-size: 13px;
  font-weight: 500;
  color: var(--text-secondary);
}

.tags-count {
  font-size: 10px;
  color: var(--text-faint);
  background: var(--bg-primary);
  padding: 1px 6px;
  border-radius: 10px;
  min-width: 20px;
  text-align: center;
  font-weight: 500;
}

.tags-content {
  padding: 0 6px 4px;
  max-height: 200px;
  overflow-y: auto;
}

.tags-loading,
.tags-empty {
  text-align: center;
  color: var(--text-faint);
  font-size: 12px;
  padding: 12px 8px;
}

.tags-clear-btn {
  width: 100%;
  padding: 4px 10px;
  border: none;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-faint);
  font-size: 11px;
  cursor: pointer;
  transition: all var(--duration-fast);
  text-align: center;
  margin-bottom: 4px;
  font-family: inherit;
}

.tags-clear-btn:hover {
  background: rgba(122, 162, 247, 0.08);
  color: var(--accent);
}

.tags-cloud {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  padding: 2px 0;
}

.tag-item {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 3px 8px;
  border: 1px solid var(--border);
  border-radius: 12px;
  background: transparent;
  color: var(--text-muted);
  font-size: 11px;
  cursor: pointer;
  transition: all var(--duration-fast) var(--ease-out);
  font-family: inherit;
  white-space: nowrap;
}

.tag-item:hover {
  background: var(--purple-glow);
  border-color: rgba(187, 154, 247, 0.3);
  color: var(--purple);
}

.tag-item.active {
  background: var(--purple-glow);
  border-color: var(--purple);
  color: var(--purple);
}

.tag-name {
  font-weight: 500;
}

.tag-count {
  font-size: 9px;
  color: var(--text-faint);
  background: var(--bg-primary);
  padding: 0 4px;
  border-radius: 8px;
  min-width: 14px;
  text-align: center;
}

.tag-item.active .tag-count {
  background: rgba(187, 154, 247, 0.15);
  color: var(--purple);
}

/* ─── Trash Section ─── */
.trash-section {
  border-top: 1px solid var(--border);
  padding: 4px 0;
  flex-shrink: 0;
}

.trash-header {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px 12px;
  cursor: pointer;
  border-radius: var(--radius-sm);
  margin: 2px 6px;
  transition: background var(--duration-fast) var(--ease-out);
}

.trash-header:hover {
  background: var(--bg-hover);
}

.trash-icon {
  font-size: 14px;
  flex-shrink: 0;
}

.trash-label {
  flex: 1;
  font-size: 13px;
  font-weight: 500;
  color: var(--text-secondary);
}

.trash-count {
  font-size: 10px;
  color: var(--text-faint);
  background: var(--bg-primary);
  padding: 1px 6px;
  border-radius: 10px;
  min-width: 20px;
  text-align: center;
  font-weight: 500;
}

.trash-content {
  padding: 0 6px 4px;
  max-height: 240px;
  overflow-y: auto;
}

.trash-loading,
.trash-empty {
  text-align: center;
  color: var(--text-faint);
  font-size: 12px;
  padding: 12px 8px;
}

.trash-action-btn {
  width: 100%;
  padding: 6px 10px;
  border: none;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-faint);
  font-size: 11px;
  cursor: pointer;
  transition: all var(--duration-fast);
  text-align: center;
  margin-bottom: 4px;
  font-family: inherit;
}

.trash-action-btn:hover {
  background: rgba(247, 118, 142, 0.08);
  color: var(--red);
}

.trash-item {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 8px;
  border-radius: var(--radius-sm);
  margin: 1px 0;
  transition: background var(--duration-fast);
}

.trash-item:hover {
  background: var(--bg-hover);
}

.trash-item-info {
  flex: 1;
  min-width: 0;
}

.trash-item-title {
  font-size: 12px;
  color: var(--text-muted);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.trash-item-meta {
  font-size: 10px;
  color: var(--text-faint);
  margin-top: 1px;
}

.trash-item-actions {
  display: flex;
  gap: 2px;
  opacity: 0;
  transition: opacity var(--duration-fast);
}

.trash-item:hover .trash-item-actions {
  opacity: 1;
}

.trash-btn {
  width: 24px;
  height: 24px;
  border: none;
  border-radius: 4px;
  background: transparent;
  cursor: pointer;
  font-size: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: background var(--duration-fast);
  padding: 0;
}

.trash-btn.restore:hover {
  background: rgba(158, 206, 138, 0.15);
}

.trash-btn.delete-perm:hover {
  background: rgba(247, 118, 142, 0.15);
}
</style>

<!-- 全局样式用 unscoped -->
<style>
/* ─── Context Menu ─── */
.context-menu {
  position: fixed;
  background: var(--bg-primary, #1a1b26);
  border: 1px solid var(--border, #292e42);
  border-radius: var(--radius-md, 10px);
  padding: 4px;
  min-width: 172px;
  box-shadow: var(--shadow-xl, 0 16px 48px rgba(0,0,0,0.6));
  z-index: 9999;
  animation: ctxIn 0.15s cubic-bezier(0.16, 1, 0.3, 1);
  backdrop-filter: blur(20px);
}

@keyframes ctxIn {
  from { opacity: 0; transform: scale(0.92); }
  to { opacity: 1; transform: scale(1); }
}

.ctx-item {
  display: flex;
  align-items: center;
  gap: 10px;
  width: 100%;
  padding: 8px 14px;
  border: none;
  background: transparent;
  color: var(--text-primary, #c0caf5);
  font-size: 13px;
  cursor: pointer;
  border-radius: var(--radius-sm, 6px);
  transition: background 0.1s;
  font-family: inherit;
}

.ctx-item:hover {
  background: var(--bg-active, #292e42);
}

.ctx-item.danger {
  color: var(--red, #f7768e);
}

.ctx-item.danger:hover {
  background: var(--red-glow, rgba(247, 118, 142, 0.1));
}

.ctx-icon {
  font-size: 14px;
  width: 20px;
  text-align: center;
  flex-shrink: 0;
}

/* ─── Modal ─── */
.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.55);
  backdrop-filter: blur(6px);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 10000;
  animation: overlayIn 0.2s ease-out;
}

@keyframes overlayIn {
  from { opacity: 0; }
  to { opacity: 1; }
}

.modal-box {
  background: var(--bg-primary, #1a1b26);
  border: 1px solid var(--border, #292e42);
  border-radius: var(--radius-xl, 18px);
  padding: 28px;
  min-width: 360px;
  box-shadow: var(--shadow-xl, 0 16px 48px rgba(0,0,0,0.6));
  animation: modalIn 0.25s cubic-bezier(0.16, 1, 0.3, 1);
}

@keyframes modalIn {
  from { opacity: 0; transform: scale(0.94) translateY(10px); }
  to { opacity: 1; transform: scale(1) translateY(0); }
}

.modal-title {
  font-size: 17px;
  font-weight: 600;
  color: var(--text-primary, #c0caf5);
  margin-bottom: 18px;
  letter-spacing: -0.3px;
}

.modal-body {
  font-size: 14px;
  color: var(--text-secondary, #a9b1d6);
  margin-bottom: 18px;
  line-height: 1.65;
}

.modal-body strong {
  color: var(--text-primary, #c0caf5);
}

.modal-field {
  margin-bottom: 16px;
}

.modal-field label {
  display: block;
  font-size: 12px;
  color: var(--text-muted, #565f89);
  margin-bottom: 7px;
  font-weight: 500;
  letter-spacing: 0.3px;
}

.modal-input {
  width: 100%;
  padding: 10px 14px;
  background: var(--bg-secondary, #16161e);
  border: 1px solid var(--border, #292e42);
  border-radius: var(--radius-sm, 6px);
  color: var(--text-primary, #c0caf5);
  font-size: 14px;
  outline: none;
  box-sizing: border-box;
  transition: border-color var(--duration-fast, 0.12s), box-shadow var(--duration-fast, 0.12s);
  font-family: inherit;
}

.modal-input:focus {
  border-color: var(--accent, #7aa2f7);
  box-shadow: 0 0 0 3px var(--accent-glow, rgba(122, 162, 247, 0.12));
}

.modal-select {
  width: 100%;
  padding: 10px 14px;
  background: var(--bg-secondary, #16161e);
  border: 1px solid var(--border, #292e42);
  border-radius: var(--radius-sm, 6px);
  color: var(--text-primary, #c0caf5);
  font-size: 14px;
  outline: none;
  box-sizing: border-box;
  cursor: pointer;
  font-family: inherit;
}

.modal-select:focus {
  border-color: var(--accent, #7aa2f7);
  box-shadow: 0 0 0 3px var(--accent-glow, rgba(122, 162, 247, 0.12));
}

.modal-select option {
  background: var(--bg-secondary, #16161e);
  color: var(--text-primary, #c0caf5);
}

.modal-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 22px;
}

.btn-cancel {
  padding: 9px 18px;
  border: 1px solid var(--border, #292e42);
  border-radius: var(--radius-sm, 6px);
  background: transparent;
  color: var(--text-secondary, #a9b1d6);
  font-size: 13px;
  cursor: pointer;
  transition: all 0.15s;
  font-family: inherit;
}

.btn-cancel:hover {
  background: var(--bg-hover, #1f2335);
  color: var(--text-primary, #c0caf5);
}

.btn-primary {
  padding: 9px 20px;
  border: none;
  border-radius: var(--radius-sm, 6px);
  background: var(--accent, #7aa2f7);
  color: #1a1b26;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s;
  font-family: inherit;
}

.btn-primary:hover {
  background: var(--accent-hover, #89b4fa);
  box-shadow: 0 2px 8px rgba(122, 162, 247, 0.25);
}

.btn-primary:active {
  transform: scale(0.97);
}

.btn-primary:disabled {
  opacity: 0.35;
  cursor: not-allowed;
  box-shadow: none;
}

.btn-danger {
  padding: 9px 20px;
  border: none;
  border-radius: var(--radius-sm, 6px);
  background: var(--red, #f7768e);
  color: #1a1b26;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s;
  font-family: inherit;
}

.btn-danger:hover {
  background: #ff9eb0;
  box-shadow: 0 2px 8px rgba(247, 118, 142, 0.25);
}

.btn-danger:active {
  transform: scale(0.97);
}
</style>
