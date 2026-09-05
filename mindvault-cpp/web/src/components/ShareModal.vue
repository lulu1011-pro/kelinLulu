<script setup lang="ts">
import { ref, watch, computed } from 'vue'

interface ShareLink {
  id: number
  note_id: number
  share_code: string
  permission: string
  created_at: string
}

const props = defineProps<{
  visible: boolean
  noteId: number
  noteTitle: string
}>()

const emit = defineEmits<{
  close: []
}>()

const shareLinks = ref<ShareLink[]>([])
const loading = ref(false)
const permission = ref('view')
const creating = ref(false)
const newShareUrl = ref('')
const origin = computed(() => {
  if (typeof window === 'undefined') return ''
  const host = window.location.hostname
  const port = window.location.port
  // 如果是 localhost，尝试用局域网 IP 替换（方便手机访问）
  if (host === 'localhost' || host === '127.0.0.1') {
    // 保留原样，用户可手动修改 IP
    return `http://${host}:${port}`
  }
  return `${window.location.protocol}//${host}:${port}`
})

watch(() => props.visible, (v) => {
  if (v) loadShares()
})

async function loadShares() {
  loading.value = true
  try {
    const res = await fetch('/api/share/list', {
      headers: {
        'Authorization': `Bearer ${localStorage.getItem('token')}`
      }
    })
    const data = await res.json()
    if (data.ok) {
      shareLinks.value = data.data || []
    }
  } catch (e) {
    console.error('Failed to load shares:', e)
  } finally {
    loading.value = false
  }
}

async function createShare() {
  creating.value = true
  try {
    const res = await fetch('/api/share', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${localStorage.getItem('token')}`
      },
      body: JSON.stringify({
        note_id: props.noteId,
        permission: permission.value,
      })
    })
    const data = await res.json()
    if (data.ok) {
      newShareUrl.value = `${origin.value}/share/${data.data.share_code}`
      await loadShares()
    }
  } catch (e) {
    console.error('Failed to create share:', e)
  } finally {
    creating.value = false
  }
}

async function deleteShare(id: number) {
  try {
    await fetch(`/api/share/${id}`, {
      method: 'DELETE',
      headers: {
        'Authorization': `Bearer ${localStorage.getItem('token')}`
      }
    })
    await loadShares()
  } catch (e) {
    console.error('Failed to delete share:', e)
  }
}

function copyUrl(url: string) {
  navigator.clipboard.writeText(url).catch(() => {
    const textarea = document.createElement('textarea')
    textarea.value = url
    document.body.appendChild(textarea)
    textarea.select()
    document.execCommand('copy')
    document.body.removeChild(textarea)
  })
}
</script>

<template>
  <Teleport to="body">
    <Transition name="overlay">
      <div v-if="visible" class="share-overlay" @click.self="emit('close')">
        <Transition name="modal">
          <div v-if="visible" class="share-modal">
            <div class="share-header">
              <span class="share-icon">🔗</span>
              <span class="share-title">分享笔记</span>
              <button class="btn-close" @click="emit('close')">×</button>
            </div>

            <div class="share-body">
              <div class="note-info">
                <span class="note-icon">📄</span>
                <span class="note-name">{{ noteTitle }}</span>
              </div>

              <!-- 创建分享 -->
              <div class="create-section">
                <div class="permission-select">
                  <label>权限：</label>
                  <select v-model="permission">
                    <option value="view">只读</option>
                    <option value="edit">可编辑</option>
                  </select>
                </div>
                <button class="btn-create" @click="createShare" :disabled="creating">
                  {{ creating ? '创建中...' : '创建分享链接' }}
                </button>
              </div>

              <!-- 新创建的链接 -->
              <div v-if="newShareUrl" class="new-share">
                <div class="share-url">{{ newShareUrl }}</div>
                <button class="btn-copy" @click="copyUrl(newShareUrl)">复制</button>
              </div>
              <div v-if="newShareUrl && newShareUrl.includes('localhost')" class="mobile-hint">
                📱 手机访问请将 <code>localhost</code> 替换为电脑的局域网 IP（如 192.168.1.104）
              </div>

              <!-- 已有分享链接 -->
              <div class="share-list">
                <h4>已分享的链接</h4>
                <div v-if="loading" class="loading">加载中...</div>
                <div v-else-if="shareLinks.length === 0" class="empty">暂无分享链接</div>
                <div v-else class="share-items">
                  <div v-for="share in shareLinks" :key="share.id" class="share-item">
                    <div class="share-info">
                      <span class="share-code">{{ share.share_code }}</span>
                      <span class="share-permission">{{ share.permission === 'view' ? '只读' : '可编辑' }}</span>
                    </div>
                    <div class="share-actions">
                      <button class="btn-copy" @click="copyUrl(`${origin}/share/${share.share_code}`)">复制</button>
                      <button class="btn-delete" @click="deleteShare(share.id)">删除</button>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </Transition>
      </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.share-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.share-modal {
  width: 500px;
  max-width: 90vw;
  max-height: 80vh;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  box-shadow: var(--shadow-xl);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.share-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 16px 20px;
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}

.share-icon { font-size: 18px; }

.share-title {
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
}

.btn-close:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

.share-body {
  flex: 1;
  overflow-y: auto;
  padding: 16px 20px;
}

.note-info {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 12px;
  background: var(--bg-primary);
  border-radius: var(--radius-sm);
  margin-bottom: 16px;
}

.note-icon { font-size: 16px; }

.note-name {
  font-size: 14px;
  color: var(--text-primary);
}

.create-section {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 16px;
}

.permission-select {
  display: flex;
  align-items: center;
  gap: 8px;
}

.permission-select label {
  font-size: 13px;
  color: var(--text-muted);
}

.permission-select select {
  padding: 6px 12px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--bg-primary);
  color: var(--text-primary);
  font-size: 13px;
}

.btn-create {
  padding: 8px 16px;
  border: 1px solid var(--accent);
  border-radius: var(--radius-sm);
  background: var(--accent);
  color: #fff;
  font-size: 13px;
  cursor: pointer;
  transition: all var(--duration-fast);
}

.btn-create:hover:not(:disabled) {
  background: var(--accent-hover);
}

.btn-create:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.new-share {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 12px;
  background: var(--accent-glow);
  border: 1px solid rgba(122, 162, 247, 0.2);
  border-radius: var(--radius-sm);
  margin-bottom: 16px;
}

.share-url {
  flex: 1;
  font-size: 12px;
  color: var(--accent);
  word-break: break-all;
}

.btn-copy {
  padding: 4px 12px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-secondary);
  font-size: 12px;
  cursor: pointer;
  transition: all var(--duration-fast);
}

.btn-copy:hover {
  background: var(--bg-hover);
}

.share-list h4 {
  font-size: 13px;
  color: var(--text-muted);
  margin: 0 0 12px 0;
}

.loading, .empty {
  text-align: center;
  color: var(--text-muted);
  font-size: 13px;
  padding: 20px;
}

.share-items {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.share-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 12px;
  background: var(--bg-primary);
  border-radius: var(--radius-sm);
}

.share-info {
  display: flex;
  align-items: center;
  gap: 8px;
}

.share-code {
  font-size: 12px;
  font-family: monospace;
  color: var(--accent);
}

.share-permission {
  font-size: 11px;
  color: var(--text-muted);
  padding: 2px 6px;
  background: var(--bg-active);
  border-radius: 4px;
}

.share-actions {
  display: flex;
  gap: 8px;
}

.btn-delete {
  padding: 4px 12px;
  border: 1px solid var(--red);
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--red);
  font-size: 12px;
  cursor: pointer;
  transition: all var(--duration-fast);
}

.btn-delete:hover {
  background: var(--red-glow);
}
</style>
