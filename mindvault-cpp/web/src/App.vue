<script setup lang="ts">
import { ref, computed, onMounted, onBeforeUnmount, watch } from 'vue'
import { notesApi, searchApi, tagsApi, type Note } from './api'
import Sidebar from './components/Sidebar.vue'
import Editor from './components/Editor.vue'
import BacklinksPanel from './components/BacklinksPanel.vue'
import SearchModal from './components/SearchModal.vue'
import SettingsModal from './components/SettingsModal.vue'
import TabBar from './components/TabBar.vue'
import AIChatPanel from './components/AIChatPanel.vue'
import KnowledgeGraph from './components/KnowledgeGraph.vue'

// ─── State ───
const notes = ref<Note[]>([])
const selectedNote = ref<Note | null>(null)
const searchQuery = ref('')
const searchResults = ref<Note[]>([])
const isSearching = ref(false)
const showSearchModal = ref(false)
const showBacklinks = ref(true)
const filterTag = ref<string | null>(null)
const filteredNotes = ref<Note[]>([])
const isLoadingTagNotes = ref(false)

// ─── Settings ───
const showSettings = ref(false)
const theme = ref<'dark' | 'light'>((localStorage.getItem('mindvault-theme') as 'dark' | 'light') || 'dark')

// ─── AI Chat ───
const showAIChat = ref(false)

// ─── Knowledge Graph ───
const showGraph = ref(false)

function applyTheme(t: 'dark' | 'light') {
  document.documentElement.setAttribute('data-theme', t)
  localStorage.setItem('mindvault-theme', t)
  theme.value = t
}

function updateTheme(t: 'dark' | 'light') {
  applyTheme(t)
}

// ─── Tabs ───
const openTabs = ref<Note[]>([])
const MAX_TABS = 10

function addTab(note: Note) {
  const existing = openTabs.value.find(t => t.id === note.id)
  if (!existing) {
    if (openTabs.value.length >= MAX_TABS) {
      openTabs.value.shift()
    }
    openTabs.value.push({ ...note })
  }
}

function closeTab(id: number) {
  const idx = openTabs.value.findIndex(t => t.id === id)
  openTabs.value = openTabs.value.filter(t => t.id !== id)
  if (selectedNote.value?.id === id) {
    if (openTabs.value.length > 0) {
      const newIdx = Math.min(idx, openTabs.value.length - 1)
      selectNote(openTabs.value[newIdx])
    } else {
      selectedNote.value = null
    }
  }
}

function selectTab(note: Note) {
  selectNote(note)
}

// ─── Load notes list ───
async function loadNotes() {
  try {
    notes.value = await notesApi.list()
    if (filterTag.value) {
      await filterByTag(filterTag.value)
    }
  } catch (e) {
    console.error('Failed to load notes:', e)
  }
}

// ─── Tag filtering ───
async function filterByTag(tagName: string | null) {
  filterTag.value = tagName
  if (!tagName) {
    filteredNotes.value = []
    return
  }
  isLoadingTagNotes.value = true
  try {
    filteredNotes.value = await tagsApi.getNotesByTag(tagName)
  } catch (e) {
    console.error('Failed to filter by tag:', e)
    filteredNotes.value = []
  } finally {
    isLoadingTagNotes.value = false
  }
}

// 当前显示的笔记列表
const displayedNotes = computed(() => {
  if (filterTag.value) return filteredNotes.value
  return notes.value
})

// ─── Select a note ───
async function selectNote(note: Note | null) {
  if (!note) return
  try {
    const full = await notesApi.get(note.id)
    selectedNote.value = full
    isSearching.value = false
    addTab(full)
  } catch (e) {
    console.error('Failed to load note:', e)
  }
}

// ─── Create new note (with title + folder) ───
async function createNote(title: string, folder: string) {
  try {
    const note = await notesApi.create({
      title: title || '无标题',
      content: '',
      folder: folder || 'default',
    })
    await loadNotes()
    selectedNote.value = note
  } catch (e) {
    console.error('Failed to create note:', e)
  }
}

// ─── Save note ───
async function saveNote(id: number, title: string, content: string) {
  try {
    const updated = await notesApi.update(id, { title, content })
    selectedNote.value = updated
    await loadNotes()
  } catch (e) {
    console.error('Failed to save note:', e)
  }
}

// ─── Delete note ───
async function deleteNote(id: number) {
  try {
    await notesApi.delete(id)
    if (selectedNote.value?.id === id) {
      selectedNote.value = null
    }
    await loadNotes()
  } catch (e) {
    console.error('Failed to delete note:', e)
  }
}

// ─── Rename note ───
async function renameNote(id: number, newTitle: string) {
  try {
    const note = await notesApi.get(id)
    const updated = await notesApi.update(id, {
      title: newTitle,
      content: note.content || '',
    })
    if (selectedNote.value?.id === id) {
      selectedNote.value = updated
    }
    await loadNotes()
  } catch (e) {
    console.error('Failed to rename note:', e)
  }
}

// ─── Move note to folder ───
async function moveNote(id: number, targetFolder: string) {
  try {
    const note = await notesApi.get(id)
    const updated = await notesApi.update(id, {
      title: note.title,
      content: note.content || '',
      folder: targetFolder,
    })
    if (selectedNote.value?.id === id) {
      selectedNote.value = updated
    }
    await loadNotes()
  } catch (e) {
    console.error('Failed to move note:', e)
  }
}

// ─── Create folder ───
async function createFolder(folderPath: string) {
  try {
    await notesApi.create({
      title: `.folder-${folderPath.split('/').pop()}`,
      content: `# ${folderPath.split('/').pop()}\n\n这是文件夹「${folderPath}」的占位笔记，可以删除。`,
      folder: folderPath,
    })
    await loadNotes()
  } catch (e) {
    console.error('Failed to create folder:', e)
  }
}

// ─── Wiki 链接导航 ───
async function navigateToNote(title: string) {
  try {
    // 优先从已加载的笔记列表中精确匹配（最快最可靠）
    const found = notes.value.find(n => n.title === title)
    if (found) {
      await selectNote(found)
      return
    }

    // 列表中没找到，用搜索 API 模糊查找
    const results = await searchApi.byTitle(title)
    const exact = results.find(n => n.title === title)
    if (exact) {
      await selectNote(exact)
      return
    }

    // 也没找到，说明笔记不存在 → 自动创建
    const note = await notesApi.create({
      title,
      content: `# ${title}\n\n`,
      folder: 'default',
    })
    await loadNotes()
    selectedNote.value = note
  } catch (e) {
    console.error('Wiki link navigation failed:', e)
  }
}

// ─── Search ───
async function doSearch(query: string) {
  if (!query.trim()) {
    isSearching.value = false
    searchResults.value = []
    return
  }
  try {
    searchResults.value = await searchApi.search(query)
    isSearching.value = true
  } catch (e) {
    console.error('Search failed:', e)
  }
}

let searchTimer: ReturnType<typeof setTimeout> | null = null
watch(searchQuery, (q) => {
  if (searchTimer) clearTimeout(searchTimer)
  searchTimer = setTimeout(() => doSearch(q), 300)
})

// ─── Ctrl+K 全局搜索 ───
function handleGlobalKeydown(e: KeyboardEvent) {
  if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
    e.preventDefault()
    showSearchModal.value = !showSearchModal.value
  }
}

onMounted(() => {
  loadNotes()
  document.addEventListener('keydown', handleGlobalKeydown)
  applyTheme(theme.value)
})
onBeforeUnmount(() => {
  document.removeEventListener('keydown', handleGlobalKeydown)
})
</script>

<template>
  <div class="app-layout">
    <!-- Sidebar -->
    <Sidebar
      :notes="displayedNotes"
      :selected-id="selectedNote?.id"
      :search-query="searchQuery"
      :search-results="searchResults"
      :is-searching="isSearching"
      :is-loading-tag="isLoadingTagNotes"
      :active-tag="filterTag"
      @select="selectNote"
      @create="createNote"
      @delete="deleteNote"
      @rename="renameNote"
      @move="moveNote"
      @search="searchQuery = $event"
      @create-folder="createFolder"
      @refresh="loadNotes"
      @filter-tag="filterByTag"
    />

    <!-- Main Area -->
    <div v-if="selectedNote" class="main-area">
      <TabBar
        :tabs="openTabs"
        :active-id="selectedNote?.id ?? null"
        @select="selectTab"
        @close="closeTab"
      />
      <div class="main-content">
        <Editor
          :note="selectedNote"
          @save="saveNote"
          @navigate="navigateToNote"
          @open-settings="showSettings = true"
        />
        <!-- 反向链接面板 -->
        <BacklinksPanel
          v-if="showBacklinks"
          :note-id="selectedNote.id"
          @navigate="selectNote"
        />
      </div>
      <!-- 收起/展开反向链接 -->
      <button
        class="toggle-backlinks"
        :class="{ collapsed: !showBacklinks }"
        @click="showBacklinks = !showBacklinks"
        :title="showBacklinks ? '收起链接面板' : '展开链接面板'"
      >
        {{ showBacklinks ? '›' : '‹' }}
      </button>
    </div>

    <!-- Empty State -->
    <div v-else class="empty-state">
      <div class="empty-icon">🧠</div>
      <h2>MindVault</h2>
      <p>选择笔记或创建新笔记开始</p>
      <div class="empty-actions">
        <button class="btn-start" @click="createNote('无标题', 'default')">📝 新建笔记</button>
        <button class="btn-settings" @click="showSettings = true" title="设置">⚙️</button>
      </div>
      <p class="shortcut-hint">按 <kbd>Ctrl+K</kbd> 快速搜索</p>
    </div>

    <!-- Ctrl+K 搜索弹框 -->
    <SearchModal
      :visible="showSearchModal"
      @close="showSearchModal = false"
      @select="selectNote"
    />

    <!-- 设置弹框 -->
    <SettingsModal
      :visible="showSettings"
      :theme="theme"
      @close="showSettings = false"
      @update-theme="updateTheme"
    />

    <!-- AI 对话面板 -->
    <AIChatPanel
      :visible="showAIChat"
      @close="showAIChat = false"
    />

    <!-- 知识图谱 -->
    <KnowledgeGraph
      :visible="showGraph"
      @close="showGraph = false"
      @select-note="selectNote"
    />

    <!-- 悬浮按钮 -->
    <div class="fab-container">
      <button class="fab-btn fab-graph" @click="showGraph = true" title="知识图谱">🕸️</button>
      <button class="fab-btn fab-ai" @click="showAIChat = true" title="AI 助手">🤖</button>
    </div>
  </div>
</template>

<style scoped>
.app-layout {
  display: flex;
  height: 100vh;
  background: var(--bg-primary);
  color: var(--text-primary);
}

.main-area {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  position: relative;
}

.main-content {
  flex: 1;
  display: flex;
  overflow: hidden;
}

.toggle-backlinks {
  position: absolute;
  right: 238px;
  top: 50%;
  transform: translateY(-50%);
  width: 18px;
  height: 48px;
  border: 1px solid var(--border);
  border-right: none;
  border-radius: 6px 0 0 6px;
  background: var(--bg-secondary);
  color: var(--text-muted);
  cursor: pointer;
  font-size: 14px;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all var(--duration-fast) var(--ease-out);
  z-index: 10;
}

.toggle-backlinks:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

.toggle-backlinks.collapsed {
  right: 0;
  border-radius: 6px 0 0 6px;
}

.empty-state {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 16px;
  position: relative;
  overflow: hidden;
}

.empty-state::before {
  content: '';
  position: absolute;
  width: 400px;
  height: 400px;
  border-radius: 50%;
  background: radial-gradient(circle, var(--accent-glow) 0%, transparent 70%);
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  pointer-events: none;
  animation: emptyPulse 4s ease-in-out infinite;
}

@keyframes emptyPulse {
  0%, 100% { opacity: 0.4; transform: translate(-50%, -50%) scale(1); }
  50% { opacity: 0.7; transform: translate(-50%, -50%) scale(1.05); }
}

.empty-icon {
  font-size: 64px;
  position: relative;
  z-index: 1;
  filter: drop-shadow(0 4px 12px rgba(122, 162, 247, 0.2));
}

.empty-state h2 {
  margin: 0;
  font-size: 28px;
  font-weight: 700;
  color: var(--accent);
  position: relative;
  z-index: 1;
  letter-spacing: -0.5px;
}

.empty-state p {
  margin: 0;
  font-size: 14px;
  color: var(--text-muted);
  position: relative;
  z-index: 1;
}

.btn-start {
  margin-top: 8px;
  padding: 10px 28px;
  border: 1px solid var(--accent);
  border-radius: var(--radius-md);
  background: var(--accent-subtle);
  color: var(--accent);
  font-size: 14px;
  font-weight: 500;
  cursor: pointer;
  transition: all var(--duration-normal) var(--ease-out);
  position: relative;
  z-index: 1;
  backdrop-filter: blur(8px);
}

.btn-start:hover {
  background: var(--accent-glow);
  transform: translateY(-2px);
  box-shadow: var(--shadow-glow);
}

.btn-start:active {
  transform: translateY(0);
}

.empty-actions {
  display: flex;
  gap: 8px;
  align-items: center;
  position: relative;
  z-index: 1;
}

.btn-settings {
  width: 40px;
  height: 40px;
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  background: var(--bg-secondary);
  color: var(--text-muted);
  font-size: 18px;
  cursor: pointer;
  transition: all var(--duration-normal) var(--ease-out);
  display: flex;
  align-items: center;
  justify-content: center;
}

.btn-settings:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
  border-color: var(--text-muted);
}

.shortcut-hint {
  font-size: 12px !important;
  color: var(--text-faint) !important;
  margin-top: -4px !important;
}

.shortcut-hint kbd {
  font-size: 11px;
  padding: 1px 5px;
  background: var(--bg-active);
  border-radius: 3px;
  font-family: inherit;
}

.fab-container {
  position: fixed;
  bottom: 24px;
  right: 24px;
  display: flex;
  flex-direction: column;
  gap: 8px;
  z-index: 100;
}

.fab-btn {
  width: 48px;
  height: 48px;
  border-radius: 50%;
  border: 1px solid var(--border);
  background: var(--bg-secondary);
  font-size: 24px;
  cursor: pointer;
  box-shadow: var(--shadow-md);
  transition: all var(--duration-normal) var(--ease-out);
  display: flex;
  align-items: center;
  justify-content: center;
}

.fab-btn:hover {
  transform: scale(1.1);
  box-shadow: var(--shadow-lg);
  border-color: var(--accent);
}
</style>
