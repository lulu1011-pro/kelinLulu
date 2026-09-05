<script setup lang="ts">
import { ref } from 'vue'
import { type Note } from '../api'

export interface FolderNode {
  name: string
  path: string
  expanded: boolean
  children: FolderNode[]
  notes: Note[]
}

const props = defineProps<{
  folder: FolderNode
  selectedId?: number
  collapsed: Set<string>
  activeFolder: string
  depth: number
}>()

const emit = defineEmits<{
  selectNote: [note: Note | null]
  toggleFolder: [path: string]
  folderContext: [e: MouseEvent, path: string]
  noteContext: [e: MouseEvent, note: Note]
  noteDrop: [e: DragEvent, note: Note]
  noteDragStart: [e: DragEvent, note: Note]
  folderDrop: [e: DragEvent, folderPath: string]
}>()

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

// ─── 拖拽相关 ───
const dragOverId = ref<number | null>(null)
const isDragOverFolder = ref(false)

function onNoteDragOver(_e: DragEvent, note: Note) {
  dragOverId.value = note.id
}

function onNoteDragLeave() {
  dragOverId.value = null
}

function onFolderDragOver() {
  isDragOverFolder.value = true
}

function onFolderDragLeave() {
  isDragOverFolder.value = false
}

function onFolderDrop(e: DragEvent) {
  isDragOverFolder.value = false
  emit('folderDrop', e, props.folder.path)
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
</script>

<template>
  <div :style="{ paddingLeft: (depth + 1) * 12 + 'px' }">
    <div
      class="folder-item"
      :class="{ active: activeFolder === folder.path, 'drag-over': isDragOverFolder }"
      @click="emit('toggleFolder', folder.path)"
      @contextmenu.prevent="emit('folderContext', $event, folder.path)"
      @dragover.prevent="onFolderDragOver"
      @dragleave="onFolderDragLeave"
      @drop.prevent="onFolderDrop($event)"
    >
      <span class="folder-toggle" @click.stop="emit('toggleFolder', folder.path)">
        {{ collapsed.has(folder.path) ? '▶' : '▼' }}
      </span>
      <span class="folder-icon">{{ getFolderIcon(folder.path) }}</span>
      <span class="folder-name">{{ folder.name }}</span>
      <span class="folder-count">{{ folder.notes.length }}</span>
    </div>
    <div v-show="!collapsed.has(folder.path)" class="folder-content">
      <!-- 子文件夹递归 -->
      <FolderTree
        v-for="child in folder.children"
        :key="child.path"
        :folder="child"
        :selected-id="selectedId"
        :collapsed="collapsed"
        :active-folder="activeFolder"
        :depth="depth + 1"
        @select-note="(n) => emit('selectNote', n)"
        @toggle-folder="(p) => emit('toggleFolder', p)"
        @folder-context="(e, p) => emit('folderContext', e, p)"
        @note-context="(e, n) => emit('noteContext', e, n)"
        @note-drop="(e, n) => emit('noteDrop', e, n)"
        @note-drag-start="(e, n) => emit('noteDragStart', e, n)"
        @folder-drop="(e, p) => emit('folderDrop', e, p)"
      />
      <!-- 笔记列表 -->
      <div
        v-for="note in folder.notes"
        :key="note.id"
        class="note-item"
        :class="{ active: note.id === selectedId, 'drag-over': dragOverId === note.id }"
        draggable="true"
        @click="emit('selectNote', note)"
        @contextmenu.prevent="emit('noteContext', $event, note)"
        @dragstart="emit('noteDragStart', $event, note)"
        @dragover.prevent="onNoteDragOver($event, note)"
        @dragleave="onNoteDragLeave"
        @drop.prevent="emit('noteDrop', $event, note)"
      >
        <span class="note-icon">📄</span>
        <div class="note-info">
          <div class="note-title">{{ note.title || '无标题' }}</div>
          <div class="note-meta">{{ formatDate(note.updated_at) }}</div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.folder-item {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 5px 12px;
  cursor: pointer;
  border-radius: var(--radius-sm, 6px);
  margin: 1px 6px;
  transition: background var(--duration-fast, 0.12s) var(--ease-out, ease-out);
}

.folder-item:hover {
  background: var(--bg-hover, #1f2335);
}

.folder-item.active {
  background: var(--accent-glow, rgba(122, 162, 247, 0.12));
}

.folder-item.drag-over {
  background: var(--accent-glow, rgba(122, 162, 247, 0.2));
  border: 1px dashed var(--accent, #7aa2f7);
}

.folder-toggle {
  font-size: 8px;
  color: var(--text-muted, #565f89);
  width: 14px;
  text-align: center;
  transition: transform var(--duration-normal, 0.2s) var(--ease-out, ease-out);
}

.folder-icon {
  font-size: 14px;
  flex-shrink: 0;
}

.folder-name {
  flex: 1;
  font-size: 13px;
  font-weight: 500;
  color: var(--text-secondary, #a9b1d6);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.folder-count {
  font-size: 10px;
  color: var(--text-faint, #3b4261);
  background: var(--bg-primary, #1a1b26);
  padding: 1px 6px;
  border-radius: 10px;
  min-width: 20px;
  text-align: center;
  font-weight: 500;
}

.folder-content {
  /* overflow hidden for collapse animation potential */
}

.note-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 7px 12px 7px 20px;
  cursor: pointer;
  border-radius: var(--radius-sm, 6px);
  margin: 1px 6px;
  transition: background var(--duration-fast, 0.12s) var(--ease-out, ease-out);
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
  background: var(--accent, #7aa2f7);
  transition: height var(--duration-normal, 0.2s) var(--ease-out, ease-out);
}

.note-item:hover {
  background: var(--bg-hover, #1f2335);
}

.note-item:hover::before {
  height: 12px;
  background: var(--text-muted, #565f89);
}

.note-item.active {
  background: var(--bg-active, #292e42);
}

.note-item.active::before {
  height: 18px;
  background: var(--accent, #7aa2f7);
  box-shadow: 0 0 6px rgba(122, 162, 247, 0.3);
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
  color: var(--text-secondary, #a9b1d6);
}

.note-item.active .note-title {
  color: var(--text-primary, #c0caf5);
}

.note-meta {
  font-size: 10px;
  color: var(--text-faint, #3b4261);
  margin-top: 2px;
}
</style>
