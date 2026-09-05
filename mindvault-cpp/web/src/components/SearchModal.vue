<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount } from 'vue'
import { searchApi, notesApi, type Note, type SearchResult, highlightToHtml } from '../api'

const props = defineProps<{ visible: boolean }>()
const emit = defineEmits<{
  close: []
  select: [note: Note]
}>()

const query = ref('')
const results = ref<(SearchResult & { _type: 'search' })[]>([])
const recentNotes = ref<Note[]>([])
const selectedIndex = ref(0)
const inputRef = ref<HTMLInputElement>()
const loading = ref(false)

// 搜索
let searchTimer: ReturnType<typeof setTimeout> | null = null
watch(query, (q) => {
  if (searchTimer) clearTimeout(searchTimer)
  if (!q.trim()) {
    results.value = []
    selectedIndex.value = 0
    return
  }
  searchTimer = setTimeout(async () => {
    loading.value = true
    try {
      const searchResults = await searchApi.search(q)
      results.value = searchResults.map(r => ({ ...r, _type: 'search' as const }))
      selectedIndex.value = 0
    } catch (e) {
      console.error('Search failed:', e)
    } finally {
      loading.value = false
    }
  }, 200)
})

// 加载最近笔记
async function loadRecent() {
  try {
    const all = await notesApi.list()
    recentNotes.value = all.slice(0, 8)
  } catch (e) {
    console.error('Failed to load recent:', e)
  }
}

watch(() => props.visible, (v) => {
  if (v) {
    query.value = ''
    results.value = []
    selectedIndex.value = 0
    loadRecent()
    setTimeout(() => inputRef.value?.focus(), 50)
  }
})

// 键盘导航
function handleKeydown(e: KeyboardEvent) {
  const items = query.value ? results.value : recentNotes.value
  if (e.key === 'ArrowDown') {
    e.preventDefault()
    selectedIndex.value = Math.min(selectedIndex.value + 1, items.length - 1)
  } else if (e.key === 'ArrowUp') {
    e.preventDefault()
    selectedIndex.value = Math.max(selectedIndex.value - 1, 0)
  } else if (e.key === 'Enter') {
    e.preventDefault()
    selectItem(selectedIndex.value)
  } else if (e.key === 'Escape') {
    emit('close')
  }
}

async function selectItem(index: number) {
  const items = query.value ? results.value : recentNotes.value
  const item = items[index]
  if (!item) return
  // 加载完整笔记
  try {
    const note = await notesApi.get(item.id)
    emit('select', note)
    emit('close')
  } catch (e) {
    console.error('Failed to load note:', e)
  }
}

// 全局快捷键
function handleGlobalKeydown(e: KeyboardEvent) {
  if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
    e.preventDefault()
    if (props.visible) {
      emit('close')
    } else {
      emit('close') // toggle 由父组件处理
    }
  }
}

onMounted(() => document.addEventListener('keydown', handleGlobalKeydown))
onBeforeUnmount(() => document.removeEventListener('keydown', handleGlobalKeydown))
</script>

<template>
  <Teleport to="body">
    <Transition name="overlay">
      <div v-if="visible" class="search-overlay" @click.self="emit('close')">
        <div class="search-modal">
          <div class="search-input-row">
            <span class="search-icon">🔍</span>
            <input
              ref="inputRef"
              v-model="query"
              class="search-input"
              placeholder="搜索笔记标题或内容..."
              @keydown="handleKeydown"
            />
            <kbd class="search-kbd">ESC</kbd>
          </div>

          <div class="search-results">
            <!-- 搜索结果 -->
            <template v-if="query">
              <div v-if="loading" class="search-hint">搜索中...</div>
              <div v-else-if="results.length === 0" class="search-hint">未找到匹配的笔记</div>
              <div
                v-for="(item, i) in results"
                :key="item.id"
                :class="['search-item', { selected: i === selectedIndex }]"
                @click="selectItem(i)"
                @mouseenter="selectedIndex = i"
              >
                <span class="item-icon">📄</span>
                <div class="item-info">
                  <div class="item-title" v-html="highlightToHtml(item.title_highlight) || item.title"></div>
                  <div
                    v-if="item.content_highlight"
                    class="item-snippet"
                    v-html="highlightToHtml(item.content_highlight)"
                  ></div>
                </div>
                <span class="item-folder">{{ item.folder }}</span>
              </div>
            </template>

            <!-- 最近笔记 -->
            <template v-else>
              <div class="search-label">最近编辑</div>
              <div
                v-for="(item, i) in recentNotes"
                :key="item.id"
                :class="['search-item', { selected: i === selectedIndex }]"
                @click="selectItem(i)"
                @mouseenter="selectedIndex = i"
              >
                <span class="item-icon">📄</span>
                <div class="item-info">
                  <div class="item-title">{{ item.title }}</div>
                </div>
                <span class="item-folder">{{ item.folder }}</span>
              </div>
            </template>
          </div>

          <div class="search-footer">
            <span><kbd>↑↓</kbd> 导航</span>
            <span><kbd>↵</kbd> 打开</span>
            <span><kbd>Ctrl+K</kbd> 切换</span>
          </div>
        </div>
      </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.search-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  backdrop-filter: blur(4px);
  display: flex;
  justify-content: center;
  padding-top: 15vh;
  z-index: 10000;
}

.search-modal {
  width: 560px;
  max-height: 480px;
  background: var(--bg-primary, #1a1b26);
  border: 1px solid var(--border, #292e42);
  border-radius: var(--radius-xl, 16px);
  box-shadow: 0 24px 64px rgba(0, 0, 0, 0.5);
  display: flex;
  flex-direction: column;
  overflow: hidden;
  animation: modalIn 0.2s cubic-bezier(0.16, 1, 0.3, 1);
}

@keyframes modalIn {
  from { opacity: 0; transform: scale(0.96) translateY(-10px); }
  to { opacity: 1; transform: scale(1) translateY(0); }
}

.search-input-row {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 14px 16px;
  border-bottom: 1px solid var(--border, #292e42);
}

.search-icon {
  font-size: 16px;
  flex-shrink: 0;
}

.search-input {
  flex: 1;
  background: transparent;
  border: none;
  color: var(--text-primary, #c0caf5);
  font-size: 15px;
  outline: none;
  font-family: inherit;
}

.search-input::placeholder {
  color: var(--text-faint, #3b4261);
}

.search-kbd {
  font-size: 10px;
  padding: 2px 6px;
  background: var(--bg-active, #292e42);
  border-radius: 4px;
  color: var(--text-muted, #565f89);
  font-family: inherit;
}

.search-results {
  flex: 1;
  overflow-y: auto;
  padding: 4px 0;
}

.search-hint {
  padding: 24px;
  text-align: center;
  color: var(--text-muted, #565f89);
  font-size: 13px;
}

.search-label {
  padding: 8px 16px 4px;
  font-size: 11px;
  font-weight: 600;
  color: var(--text-faint, #3b4261);
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.search-item {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 16px;
  cursor: pointer;
  transition: background 0.08s;
}

.search-item:hover,
.search-item.selected {
  background: var(--bg-hover, #1f2335);
}

.search-item.selected {
  background: var(--bg-active, #292e42);
}

.item-icon {
  font-size: 14px;
  flex-shrink: 0;
  opacity: 0.6;
}

.item-info {
  flex: 1;
  min-width: 0;
}

.item-title {
  font-size: 14px;
  font-weight: 500;
  color: var(--text-primary, #c0caf5);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.item-title :deep(mark) {
  background: rgba(247, 118, 142, 0.25);
  color: var(--red, #f7768e);
  padding: 0 2px;
  border-radius: 2px;
}

.item-snippet {
  font-size: 12px;
  color: var(--text-muted, #565f89);
  margin-top: 3px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.item-snippet :deep(mark) {
  background: rgba(247, 118, 142, 0.2);
  color: var(--red, #f7768e);
  padding: 0 2px;
  border-radius: 2px;
}

.item-folder {
  font-size: 11px;
  color: var(--text-faint, #3b4261);
  background: var(--bg-primary, #1a1b26);
  padding: 2px 8px;
  border-radius: 8px;
  flex-shrink: 0;
  max-width: 100px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.search-footer {
  display: flex;
  gap: 16px;
  padding: 10px 16px;
  border-top: 1px solid var(--border, #292e42);
  font-size: 11px;
  color: var(--text-faint, #3b4261);
}

.search-footer kbd {
  font-size: 10px;
  padding: 1px 4px;
  background: var(--bg-active, #292e42);
  border-radius: 3px;
  font-family: inherit;
  margin-right: 2px;
}

/* 过渡动画 */
.overlay-enter-active { transition: opacity 0.15s ease-out; }
.overlay-leave-active { transition: opacity 0.1s ease-in; }
.overlay-enter-from,
.overlay-leave-to { opacity: 0; }
</style>
