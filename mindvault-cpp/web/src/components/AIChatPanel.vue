<script setup lang="ts">
import { ref, watch, nextTick } from 'vue'
import { conversationsApi, type Conversation, type ChatMessage } from '../api'

interface Provider {
  id: string
  name: string
  url: string
  models: string[]
}

const props = defineProps<{
  visible: boolean
}>()

const emit = defineEmits<{
  close: []
}>()

// ─── 会话状态 ───
const conversations = ref<Conversation[]>([])
const activeConversationId = ref<number | null>(null)
const messages = ref<ChatMessage[]>([])
const loading = ref(false)
const showSettings = ref(false)
const editingTitle = ref<number | null>(null)
const editTitleValue = ref('')

// ─── AI Config ───
const providers = ref<Provider[]>([])
const selectedProvider = ref(localStorage.getItem('ai-provider') || 'tongyi')
const selectedModel = ref('')
const ollamaAvailable = ref(false)
const ollamaModels = ref<string[]>([])
const providerConfigs = ref<Record<string, { key: string; url: string; model: string }>>({})
const apiKey = ref('')
const customUrl = ref('')
const input = ref('')
const inputRef = ref<HTMLTextAreaElement | null>(null)

// ─── 初始化加载 ───
watch(() => props.visible, async (v) => {
  if (v) {
    await checkStatus()
    loadProviderConfig(selectedProvider.value)
    await loadConversations()
    // 恢复上次选中的会话
    const lastId = localStorage.getItem('ai-last-conversation-id')
    if (lastId && conversations.value.some(c => c.id === Number(lastId))) {
      await switchConversation(Number(lastId))
    } else if (conversations.value.length > 0) {
      await switchConversation(conversations.value[0].id)
    }
    await nextTick()
    inputRef.value?.focus()
  }
})

watch(selectedProvider, (v) => {
  loadProviderConfig(v)
  const models = getModels()
  if (models.length > 0 && !models.includes(selectedModel.value)) {
    selectedModel.value = models[0]
  }
})

// ─── 提供商配置 ───
function loadProviderConfig(provider: string) {
  const saved = localStorage.getItem(`ai-config-${provider}`)
  if (saved) {
    try {
      const config = JSON.parse(saved)
      providerConfigs.value[provider] = config
    } catch (e) {}
  }
  const config = providerConfigs.value[provider] || { key: '', url: '', model: '' }
  apiKey.value = config.key
  customUrl.value = config.url
  if (config.model) selectedModel.value = config.model
}

function saveProviderConfig() {
  const config = { key: apiKey.value, url: customUrl.value, model: selectedModel.value }
  providerConfigs.value[selectedProvider.value] = config
  localStorage.setItem(`ai-config-${selectedProvider.value}`, JSON.stringify(config))
  localStorage.setItem('ai-provider', selectedProvider.value)
  showSettings.value = false
}

async function checkStatus() {
  try {
    const res = await fetch('/api/ai/status')
    const data = await res.json()
    if (data.ok) {
      ollamaAvailable.value = data.data.ollama
      ollamaModels.value = data.data.ollama_models || []
      providers.value = data.data.providers || []
    }
  } catch (e) {
    console.error('Failed to check AI status:', e)
  }
}

function getModels(): string[] {
  if (selectedProvider.value === 'ollama') return ollamaModels.value
  const p = providers.value.find(p => p.id === selectedProvider.value)
  return p?.models || []
}

// ─── 会话操作 ───
async function loadConversations() {
  try {
    conversations.value = await conversationsApi.list()
  } catch (e) {
    console.error('Failed to load conversations:', e)
  }
}

async function createConversation() {
  try {
    const conv = await conversationsApi.create()
    conversations.value.unshift(conv)
    await switchConversation(conv.id)
  } catch (e) {
    console.error('Failed to create conversation:', e)
  }
}

async function switchConversation(id: number) {
  if (activeConversationId.value === id) return
  activeConversationId.value = id
  localStorage.setItem('ai-last-conversation-id', String(id))
  try {
    const detail = await conversationsApi.get(id)
    messages.value = detail.messages
    await nextTick()
    scrollToBottom()
  } catch (e) {
    console.error('Failed to load conversation:', e)
    messages.value = []
  }
}

async function deleteConversation(id: number) {
  try {
    await conversationsApi.delete(id)
    conversations.value = conversations.value.filter(c => c.id !== id)
    if (activeConversationId.value === id) {
      activeConversationId.value = null
      messages.value = []
      if (conversations.value.length > 0) {
        await switchConversation(conversations.value[0].id)
      }
    }
  } catch (e) {
    console.error('Failed to delete conversation:', e)
  }
}

function startRename(id: number, title: string) {
  editingTitle.value = id
  editTitleValue.value = title
}

async function finishRename() {
  if (editingTitle.value === null || !editTitleValue.value.trim()) {
    editingTitle.value = null
    return
  }
  try {
    await conversationsApi.rename(editingTitle.value, editTitleValue.value.trim())
    const conv = conversations.value.find(c => c.id === editingTitle.value)
    if (conv) conv.title = editTitleValue.value.trim()
    editingTitle.value = null
  } catch (e) {
    console.error('Failed to rename conversation:', e)
  }
}

// ─── 发送消息 ───
async function sendMessage() {
  if (!input.value.trim() || loading.value) return

  const question = input.value.trim()
  input.value = ''

  // 如果没有会话，先创建一个
  if (!activeConversationId.value) {
    try {
      const conv = await conversationsApi.create()
      conversations.value.unshift(conv)
      activeConversationId.value = conv.id
      localStorage.setItem('ai-last-conversation-id', String(conv.id))
    } catch (e) {
      console.error('Failed to create conversation:', e)
      return
    }
  }

  // 乐观更新：立即显示用户消息
  const userMsg: ChatMessage = {
    id: Date.now(),
    role: 'user',
    content: question,
    tokens: 0,
    created_at: new Date().toISOString(),
  }
  messages.value.push(userMsg)
  await nextTick()
  scrollToBottom()

  loading.value = true
  try {
    const res = await fetch('/api/ai/chat', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${localStorage.getItem('token') || ''}`,
      },
      body: JSON.stringify({
        question,
        provider: selectedProvider.value,
        model: selectedModel.value,
        api_key: apiKey.value,
        api_url: selectedProvider.value === 'custom' ? customUrl.value : '',
        conversation_id: activeConversationId.value,
      }),
    })
    const data = await res.json()
    if (data.ok) {
      const assistantMsg: ChatMessage = {
        id: data.data.message_id || Date.now(),
        role: 'assistant',
        content: data.data.answer,
        tokens: 0,
        created_at: new Date().toISOString(),
      }
      messages.value.push(assistantMsg)

      // 更新会话标题（首轮）
      const conv = conversations.value.find(c => c.id === activeConversationId.value)
      if (conv && (conv.title === '新对话' || !conv.title)) {
        conv.title = question.length > 20 ? question.substring(0, 20) : question
      }
      // 刷新列表排序
      await loadConversations()
      await nextTick()
      scrollToBottom()
    }
  } catch (e) {
    console.error('AI chat failed:', e)
    messages.value.push({
      id: Date.now() + 1,
      role: 'assistant',
      content: '请求失败，请检查后端服务。',
      tokens: 0,
      created_at: new Date().toISOString(),
    })
  } finally {
    loading.value = false
  }
}

function scrollToBottom() {
  const el = document.querySelector('.ai-messages')
  if (el) el.scrollTop = el.scrollHeight
}

function onKeydown(e: KeyboardEvent) {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault()
    sendMessage()
  }
}

function relativeTime(dateStr: string): string {
  const now = Date.now()
  const d = new Date(dateStr).getTime()
  const diff = Math.floor((now - d) / 1000)
  if (diff < 60) return '刚刚'
  if (diff < 3600) return Math.floor(diff / 60) + '分钟前'
  if (diff < 86400) return Math.floor(diff / 3600) + '小时前'
  return Math.floor(diff / 86400) + '天前'
}
</script>

<template>
  <Teleport to="body">
    <Transition name="overlay">
      <div v-if="visible" class="ai-overlay" @click.self="emit('close')">
        <Transition name="modal">
          <div v-if="visible" class="ai-panel">
            <!-- 头部 -->
            <div class="ai-header">
              <span class="ai-icon">🤖</span>
              <span class="ai-title">AI 助手</span>
              <select v-model="selectedProvider" class="provider-select" @change="selectedModel = getModels()[0] || ''">
                <option v-if="ollamaAvailable" value="ollama">Ollama</option>
                <option v-for="p in providers" :key="p.id" :value="p.id">{{ p.name }}</option>
              </select>
              <select v-model="selectedModel" class="model-select">
                <option v-for="m in getModels()" :key="m" :value="m">{{ m }}</option>
              </select>
              <button class="btn-settings" @click="showSettings = !showSettings" title="API 设置">⚙️</button>
              <button class="btn-close" @click="emit('close')">×</button>
            </div>

            <!-- API Key 设置 -->
            <div v-if="showSettings" class="settings-bar">
              <div class="setting-item">
                <label>API Key:</label>
                <input v-model="apiKey" type="password" placeholder="输入 API Key" class="setting-input" />
              </div>
              <div v-if="selectedProvider === 'custom'" class="setting-item">
                <label>API URL:</label>
                <input v-model="customUrl" type="text" placeholder="https://api.example.com/v1/chat/completions" class="setting-input" />
              </div>
              <button class="btn-save" @click="saveProviderConfig">保存</button>
            </div>

            <!-- 主体：会话列表 + 消息区 -->
            <div class="ai-body">
              <!-- 会话列表侧边栏 -->
              <div class="conversation-sidebar">
                <div class="sidebar-header">
                  <span class="sidebar-title">会话</span>
                  <button class="btn-new-conv" @click="createConversation" title="新建会话">+</button>
                </div>
                <div class="conversation-list">
                  <div v-if="conversations.length === 0" class="sidebar-empty">暂无会话</div>
                  <div
                    v-for="conv in conversations"
                    :key="conv.id"
                    class="conversation-item"
                    :class="{ active: conv.id === activeConversationId }"
                    @click="switchConversation(conv.id)"
                  >
                    <div class="conv-content">
                      <!-- 编辑标题 -->
                      <template v-if="editingTitle === conv.id">
                        <input
                          v-model="editTitleValue"
                          class="conv-title-input"
                          @blur="finishRename"
                          @keydown.enter.prevent="finishRename"
                          @keydown.esc="editingTitle = null"
                          autofocus
                        />
                      </template>
                      <template v-else>
                        <div class="conv-title" @dblclick="startRename(conv.id, conv.title)">{{ conv.title }}</div>
                      </template>
                    </div>
                    <div class="conv-actions">
                      <button class="btn-conv-delete" @click.stop="deleteConversation(conv.id)" title="删除">🗑</button>
                    </div>
                  </div>
                </div>
              </div>

              <!-- 消息区 -->
              <div class="chat-area">
                <div class="ai-messages">
                  <div v-if="messages.length === 0" class="ai-empty">
                    <span class="empty-icon">💬</span>
                    <p>基于你的知识库提问</p>
                    <p class="hint">AI 会检索相关笔记来回答</p>
                    <p class="hint" v-if="!apiKey && selectedProvider !== 'ollama'">
                      请先点击 ⚙️ 配置 API Key
                    </p>
                  </div>
                  <div
                    v-for="msg in messages"
                    :key="msg.id"
                    class="ai-message"
                    :class="msg.role"
                  >
                    <div class="msg-content">{{ msg.content }}</div>
                    <div class="msg-time">{{ relativeTime(msg.created_at) }}</div>
                  </div>
                  <div v-if="loading" class="ai-message assistant loading">
                    <div class="msg-content">思考中...</div>
                  </div>
                </div>

                <!-- 输入区 -->
                <div class="ai-input-area">
                  <textarea
                    ref="inputRef"
                    v-model="input"
                    class="ai-input"
                    placeholder="输入问题... (Enter 发送, Shift+Enter 换行)"
                    rows="2"
                    @keydown="onKeydown"
                  />
                  <button class="btn-send" @click="sendMessage" :disabled="!input.trim() || loading">
                    发送
                  </button>
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
.ai-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.ai-panel {
  width: 800px;
  max-width: 95vw;
  height: 70vh;
  max-height: 600px;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  box-shadow: var(--shadow-xl);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

/* ─── 头部 ─── */
.ai-header {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 10px 16px;
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}

.ai-icon { font-size: 18px; }

.ai-title {
  font-size: 15px;
  font-weight: 600;
  color: var(--text-primary);
}

.provider-select, .model-select {
  padding: 4px 8px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--bg-primary);
  color: var(--text-secondary);
  font-size: 11px;
  cursor: pointer;
}

.provider-select:focus, .model-select:focus {
  outline: none;
  border-color: var(--accent);
}

.btn-settings, .btn-close {
  width: 28px;
  height: 28px;
  border: none;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-muted);
  font-size: 14px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all var(--duration-fast);
}

.btn-settings:hover, .btn-close:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

/* ─── 设置栏 ─── */
.settings-bar {
  padding: 10px 16px;
  border-bottom: 1px solid var(--border);
  background: var(--bg-primary);
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.setting-item {
  display: flex;
  align-items: center;
  gap: 8px;
}

.setting-item label {
  font-size: 12px;
  color: var(--text-muted);
  min-width: 60px;
}

.setting-input {
  flex: 1;
  padding: 4px 8px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--bg-secondary);
  color: var(--text-primary);
  font-size: 12px;
}

.setting-input:focus {
  outline: none;
  border-color: var(--accent);
}

.btn-save {
  align-self: flex-end;
  padding: 4px 12px;
  border: 1px solid var(--accent);
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--accent);
  font-size: 12px;
  cursor: pointer;
}

.btn-save:hover {
  background: var(--accent-glow);
}

/* ─── 主体布局 ─── */
.ai-body {
  display: flex;
  flex: 1;
  min-height: 0;
  overflow: hidden;
}

/* ─── 会话列表侧边栏 ─── */
.conversation-sidebar {
  width: 200px;
  border-right: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  flex-shrink: 0;
}

.sidebar-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 12px;
  border-bottom: 1px solid var(--border);
}

.sidebar-title {
  font-size: 13px;
  font-weight: 600;
  color: var(--text-secondary);
}

.btn-new-conv {
  width: 24px;
  height: 24px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--accent);
  font-size: 16px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all var(--duration-fast);
}

.btn-new-conv:hover {
  background: var(--accent-glow);
  border-color: var(--accent);
}

.conversation-list {
  flex: 1;
  overflow-y: auto;
  padding: 6px;
}

.sidebar-empty {
  text-align: center;
  padding: 24px 8px;
  color: var(--text-muted);
  font-size: 12px;
}

.conversation-item {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 8px 10px;
  border-radius: var(--radius-sm);
  cursor: pointer;
  transition: background var(--duration-fast);
}

.conversation-item:hover {
  background: var(--bg-hover);
}

.conversation-item.active {
  background: var(--accent-glow);
  border: 1px solid rgba(122, 162, 247, 0.2);
}

.conv-content {
  flex: 1;
  min-width: 0;
  overflow: hidden;
}

.conv-title {
  font-size: 13px;
  color: var(--text-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.conv-title-input {
  width: 100%;
  padding: 2px 4px;
  border: 1px solid var(--accent);
  border-radius: 2px;
  background: var(--bg-primary);
  color: var(--text-primary);
  font-size: 13px;
  outline: none;
}

.conv-actions {
  opacity: 0;
  transition: opacity var(--duration-fast);
}

.conversation-item:hover .conv-actions {
  opacity: 1;
}

.btn-conv-delete {
  width: 22px;
  height: 22px;
  border: none;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-muted);
  font-size: 12px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
}

.btn-conv-delete:hover {
  background: rgba(240, 100, 100, 0.1);
  color: #f06464;
}

/* ─── 聊天区 ─── */
.chat-area {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.ai-messages {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
}

.ai-empty {
  text-align: center;
  padding: 48px 16px;
  color: var(--text-muted);
}

.ai-empty .empty-icon {
  font-size: 32px;
  display: block;
  margin-bottom: 12px;
  opacity: 0.5;
}

.ai-empty p { margin: 4px 0; font-size: 14px; }
.ai-empty .hint { font-size: 12px; color: var(--text-faint); }

.ai-message { margin-bottom: 12px; }

.ai-message.user .msg-content {
  background: var(--accent-glow);
  border: 1px solid rgba(122, 162, 247, 0.2);
  border-radius: var(--radius-md) var(--radius-md) 4px var(--radius-md);
  padding: 10px 14px;
  margin-left: 40px;
  font-size: 14px;
  color: var(--text-primary);
  white-space: pre-wrap;
}

.ai-message.assistant .msg-content {
  background: var(--bg-primary);
  border: 1px solid var(--border);
  border-radius: var(--radius-md) var(--radius-md) var(--radius-md) 4px;
  padding: 10px 14px;
  margin-right: 40px;
  font-size: 14px;
  color: var(--text-secondary);
  white-space: pre-wrap;
}

.ai-message.loading .msg-content {
  color: var(--text-muted);
  font-style: italic;
}

.msg-time {
  font-size: 10px;
  color: var(--text-faint);
  margin-top: 2px;
  padding: 0 14px;
}

.ai-message.user .msg-time { text-align: right; margin-left: 40px; }
.ai-message.assistant .msg-time { text-align: left; margin-right: 40px; }

/* ─── 输入区 ─── */
.ai-input-area {
  display: flex;
  gap: 8px;
  padding: 12px 16px;
  border-top: 1px solid var(--border);
  flex-shrink: 0;
}

.ai-input {
  flex: 1;
  padding: 8px 12px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--bg-primary);
  color: var(--text-primary);
  font-size: 14px;
  font-family: inherit;
  resize: none;
}

.ai-input:focus {
  outline: none;
  border-color: var(--accent);
}

.btn-send {
  padding: 8px 16px;
  border: none;
  border-radius: var(--radius-sm);
  background: var(--accent);
  color: #fff;
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: all var(--duration-fast);
}

.btn-send:hover:not(:disabled) {
  background: var(--accent-hover);
}

.btn-send:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

/* ─── 动画 ─── */
.overlay-enter-active, .overlay-leave-active {
  transition: opacity 0.2s ease;
}
.overlay-enter-from, .overlay-leave-to {
  opacity: 0;
}
.modal-enter-active, .modal-leave-active {
  transition: transform 0.2s ease, opacity 0.2s ease;
}
.modal-enter-from, .modal-leave-to {
  transform: scale(0.95);
  opacity: 0;
}
</style>
