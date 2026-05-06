<script setup lang="ts">
import { ref, watch } from 'vue'
import { notesApi, type Version } from '../api'

const props = defineProps<{
  noteId: number
  visible: boolean
}>()

const emit = defineEmits<{
  close: []
  restore: [content: string]
}>()

const versions = ref<Version[]>([])
const loading = ref(false)
const selectedVersion = ref<Version | null>(null)

watch(() => props.visible, (v) => {
  if (v) loadVersions()
})

async function loadVersions() {
  loading.value = true
  try {
    versions.value = await notesApi.getVersions(props.noteId)
  } catch (e) {
    console.error('Failed to load versions:', e)
  } finally {
    loading.value = false
  }
}

async function viewVersion(version: Version) {
  try {
    const full = await notesApi.getVersion(version.id)
    selectedVersion.value = full
  } catch (e) {
    console.error('Failed to load version:', e)
  }
}

function doRestore() {
  if (selectedVersion.value?.content) {
    emit('restore', selectedVersion.value.content)
    emit('close')
  }
}

function formatDate(dateStr: string): string {
  if (!dateStr) return ''
  const d = new Date(dateStr.replace(' ', 'T'))
  const now = new Date()
  const diffMs = now.getTime() - d.getTime()
  const diffMins = Math.floor(diffMs / 60000)
  const diffHours = Math.floor(diffMs / 3600000)
  const diffDays = Math.floor(diffMs / 86400000)

  if (diffMins < 1) return '刚刚'
  if (diffMins < 60) return `${diffMins}分钟前`
  if (diffHours < 24) return `${diffHours}小时前`
  if (diffDays < 7) return `${diffDays}天前`
  return dateStr.substring(0, 16)
}
</script>

<template>
  <Teleport to="body">
    <Transition name="overlay">
      <div v-if="visible" class="version-overlay" @click.self="emit('close')">
        <Transition name="modal">
          <div v-if="visible" class="version-modal">
            <div class="version-header">
              <span class="version-icon">📋</span>
              <span class="version-title">版本历史</span>
              <button class="btn-close" @click="emit('close')">×</button>
            </div>

            <div class="version-body">
              <div class="version-list">
                <div v-if="loading" class="version-loading">加载中...</div>
                <div v-else-if="versions.length === 0" class="version-empty">
                  暂无历史版本
                </div>
                <div
                  v-for="v in versions"
                  :key="v.id"
                  class="version-item"
                  :class="{ active: selectedVersion?.id === v.id }"
                  @click="viewVersion(v)"
                >
                  <span class="version-time">{{ formatDate(v.created_at) }}</span>
                  <span class="version-name">{{ v.title }}</span>
                </div>
              </div>

              <div class="version-preview">
                <div v-if="!selectedVersion" class="preview-empty">
                  选择一个版本查看
                </div>
                <template v-else>
                  <div class="preview-header">
                    <span class="preview-title">{{ selectedVersion.title }}</span>
                    <button class="btn-restore" @click="doRestore">恢复此版本</button>
                  </div>
                  <div class="preview-content">{{ selectedVersion.content }}</div>
                </template>
              </div>
            </div>
          </div>
        </Transition>
      </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.version-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.version-modal {
  width: 800px;
  max-width: 90vw;
  height: 60vh;
  max-height: 500px;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  box-shadow: var(--shadow-xl);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.version-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 16px 20px;
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}

.version-icon {
  font-size: 18px;
}

.version-title {
  flex: 1;
  font-size: 16px;
  font-weight: 600;
  color: var(--text-primary);
}

.btn-close {
  width: 28px;
  height: 28px;
  border: none;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-muted);
  font-size: 18px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all var(--duration-fast);
}

.btn-close:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

.version-body {
  flex: 1;
  display: flex;
  overflow: hidden;
}

.version-list {
  width: 240px;
  border-right: 1px solid var(--border);
  overflow-y: auto;
  flex-shrink: 0;
}

.version-loading,
.version-empty {
  text-align: center;
  color: var(--text-muted);
  font-size: 13px;
  padding: 24px 12px;
}

.version-item {
  display: flex;
  flex-direction: column;
  gap: 2px;
  padding: 10px 14px;
  cursor: pointer;
  transition: all var(--duration-fast);
  border-bottom: 1px solid var(--border-subtle);
}

.version-item:hover {
  background: var(--bg-hover);
}

.version-item.active {
  background: var(--accent-glow);
}

.version-time {
  font-size: 11px;
  color: var(--text-muted);
}

.version-name {
  font-size: 13px;
  color: var(--text-secondary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.version-item.active .version-name {
  color: var(--accent);
}

.version-preview {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.preview-empty {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--text-muted);
  font-size: 13px;
}

.preview-header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 16px;
  border-bottom: 1px solid var(--border);
}

.preview-title {
  flex: 1;
  font-size: 14px;
  font-weight: 500;
  color: var(--text-primary);
}

.btn-restore {
  padding: 6px 12px;
  border: 1px solid var(--accent);
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--accent);
  font-size: 12px;
  cursor: pointer;
  transition: all var(--duration-fast);
}

.btn-restore:hover {
  background: var(--accent-glow);
}

.preview-content {
  flex: 1;
  padding: 16px;
  overflow-y: auto;
  font-size: 13px;
  color: var(--text-secondary);
  white-space: pre-wrap;
  font-family: 'JetBrains Mono', Consolas, monospace;
}
</style>
