<script setup lang="ts">
import { ref, watch } from 'vue'
import { notesApi, getRecommendations, type Note, type Recommendation } from '../api'

const props = defineProps<{ noteId: number }>()
const emit = defineEmits<{ navigate: [note: Note] }>()

const backlinks = ref<Note[]>([])
const forwardLinks = ref<Note[]>([])
const loading = ref(false)
const activeTab = ref<'back' | 'forward' | 'rec'>('back')
const recommendations = ref<Recommendation[]>([])
const recLoading = ref(false)

async function loadLinks() {
  if (!props.noteId) return
  loading.value = true
  recLoading.value = true
  try {
    const [bl, fl, rec] = await Promise.all([
      notesApi.backlinks(props.noteId),
      notesApi.links(props.noteId),
      getRecommendations(props.noteId).catch(() => []),
    ])
    backlinks.value = bl
    forwardLinks.value = fl
    recommendations.value = rec
  } catch (e) {
    console.error('Failed to load links:', e)
  } finally {
    loading.value = false
    recLoading.value = false
  }
}

watch(() => props.noteId, loadLinks, { immediate: true })
</script>

<template>
  <div class="backlinks-panel">
    <div class="panel-header">
      <div class="tab-group">
        <button
          :class="['tab-btn', { active: activeTab === 'back' }]"
          @click="activeTab = 'back'"
        >
          ↩ 反向链接 <span v-if="backlinks.length" class="count">{{ backlinks.length }}</span>
        </button>
        <button
          :class="['tab-btn', { active: activeTab === 'forward' }]"
          @click="activeTab = 'forward'"
        >
          ↗ 正向链接 <span v-if="forwardLinks.length" class="count">{{ forwardLinks.length }}</span>
        </button>
        <button
          :class="['tab-btn', { active: activeTab === 'rec' }]"
          @click="activeTab = 'rec'"
        >
          ✨ 推荐 <span v-if="recommendations.length" class="count">{{ recommendations.length }}</span>
        </button>
      </div>
    </div>

    <div v-if="loading" class="panel-loading">加载中...</div>

    <div v-else-if="activeTab === 'back'" class="link-list">
      <div v-if="backlinks.length === 0" class="empty-links">
        <span class="empty-icon">🔗</span>
        <p>暂无反向链接</p>
        <p class="hint">其他笔记使用 [[标题]] 链接到这里时会显示</p>
      </div>
      <div
        v-for="link in backlinks"
        :key="link.id"
        class="link-item"
        @click="emit('navigate', link)"
      >
        <span class="link-icon">📄</span>
        <div class="link-info">
          <div class="link-title">{{ link.title }}</div>
          <div class="link-meta">{{ link.updated_at }}</div>
        </div>
      </div>
    </div>

    <div v-else class="link-list">
      <div v-if="forwardLinks.length === 0" class="empty-links">
        <span class="empty-icon">🔗</span>
        <p>暂无正向链接</p>
        <p class="hint">在笔记中使用 [[标题]] 链接其他笔记</p>
      </div>
      <div
        v-for="link in forwardLinks"
        :key="link.id"
        class="link-item"
        @click="emit('navigate', link)"
      >
        <span class="link-icon">📄</span>
        <div class="link-info">
          <div class="link-title">{{ link.title }}</div>
          <div class="link-meta">{{ link.updated_at }}</div>
        </div>
      </div>
    </div>

    <!-- ─── P1-7 关联推荐 ─── -->
    <div v-else-if="activeTab === 'rec'" class="link-list">
      <div v-if="recLoading" class="panel-loading">加载中...</div>
      <div v-else-if="recommendations.length === 0" class="empty-links">
        <span class="empty-icon">✨</span>
        <p>暂无推荐</p>
        <p class="hint">添加更多笔记或配置 embedding 后会有推荐</p>
      </div>
      <div
        v-for="rec in recommendations"
        :key="rec.id"
        class="link-item"
        @click="emit('navigate', { id: rec.id, title: rec.title, folder: rec.folder } as Note)"
      >
        <span class="link-icon">{{ rec.source === 'both' ? '🔗✨' : rec.source === 'linked' ? '🔗' : '✨' }}</span>
        <div class="link-info">
          <div class="link-title">{{ rec.title }}</div>
          <div class="link-meta">
            {{ rec.source === 'both' ? '链接+相似' : rec.source === 'linked' ? '已链接' : '内容相似' }}
            · 相关度 {{ (rec.score * 100).toFixed(0) }}%
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.backlinks-panel {
  width: 240px;
  min-width: 240px;
  background: var(--bg-secondary);
  border-left: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.panel-header {
  padding: 12px 12px 8px;
  border-bottom: 1px solid var(--border);
}

.tab-group {
  display: flex;
  gap: 2px;
  background: var(--bg-primary);
  border-radius: var(--radius-sm);
  padding: 2px;
}

.tab-btn {
  flex: 1;
  padding: 6px 8px;
  border: none;
  border-radius: 4px;
  background: transparent;
  color: var(--text-muted);
  font-size: 11px;
  font-weight: 500;
  cursor: pointer;
  transition: all var(--duration-fast) var(--ease-out);
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 4px;
  white-space: nowrap;
}

.tab-btn:hover {
  color: var(--text-secondary);
}

.tab-btn.active {
  background: var(--bg-active);
  color: var(--text-primary);
  box-shadow: var(--shadow-sm);
}

.count {
  font-size: 10px;
  background: var(--accent-glow);
  color: var(--accent);
  padding: 1px 5px;
  border-radius: 8px;
  font-weight: 600;
}

.panel-loading {
  padding: 24px;
  text-align: center;
  color: var(--text-muted);
  font-size: 13px;
}

.link-list {
  flex: 1;
  overflow-y: auto;
  padding: 4px 0;
}

.empty-links {
  text-align: center;
  padding: 32px 16px;
  color: var(--text-muted);
}

.empty-icon {
  font-size: 28px;
  display: block;
  margin-bottom: 8px;
  opacity: 0.5;
}

.empty-links p {
  margin: 4px 0;
  font-size: 13px;
}

.empty-links .hint {
  font-size: 11px;
  color: var(--text-faint);
}

.link-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 12px;
  cursor: pointer;
  transition: background var(--duration-fast) var(--ease-out);
  border-radius: 0;
}

.link-item:hover {
  background: var(--bg-hover);
}

.link-icon {
  font-size: 12px;
  flex-shrink: 0;
  opacity: 0.6;
}

.link-info {
  flex: 1;
  min-width: 0;
}

.link-title {
  font-size: 13px;
  font-weight: 500;
  color: var(--text-secondary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.link-item:hover .link-title {
  color: var(--accent);
}

.link-meta {
  font-size: 10px;
  color: var(--text-faint);
  margin-top: 2px;
}
</style>
