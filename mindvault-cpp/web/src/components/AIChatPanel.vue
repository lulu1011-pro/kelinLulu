<script setup lang="ts">
import { ref, watch, nextTick } from 'vue'
import { conversationsApi, chatStream, chatWithTools, type Conversation, type ChatMessage, type ChatWithToolsResult, type ToolCall, type SearchResult, type Note } from '../api'

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
  /** P0-3.2 引用溯源：点击来源笔记跳转打开 */
  selectNote: [note: Note]
}>()

// ─── 会话状态 ───
const conversations = ref<Conversation[]>([])
const activeConversationId = ref<number | null>(null)
const messages = ref<ChatMessage[]>([])
const loading = ref(false)
const isStreaming = ref(false)
const streamError = ref('')
let abortController: AbortController | null = null
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
const useTools = ref(false)  // P2 function calling 工具模式开关（默认关闭）
const toolCalls = ref<ToolCall[]>([])  // 当前消息的工具调用过程

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
// 记录最后一次发送的问题，用于重试
let lastQuestion = ''

async function sendMessage() {
  if (!input.value.trim() || loading.value || isStreaming.value) return

  const question = input.value.trim()
  input.value = ''
  lastQuestion = question
  streamError.value = ''

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
  const userMsgId = Date.now()
  const userMsg: ChatMessage = {
    id: userMsgId,
    role: 'user',
    content: question,
    tokens: 0,
    created_at: new Date().toISOString(),
  }
  messages.value.push(userMsg)

  // 创建 assistant 占位消息（流式填充内容）
  const assistantMsgId = Date.now() + 1
  const assistantMsg: ChatMessage = {
    id: assistantMsgId,
    role: 'assistant',
    content: '',
    tokens: 0,
    created_at: new Date().toISOString(),
  }
  messages.value.push(assistantMsg)
  await nextTick()
  scrollToBottom()

  loading.value = true

  // P2 工具模式：不走流式，走 chatWithTools 单轮 tool loop
  if (useTools.value) {
    toolCalls.value = []
    isStreaming.value = false
    try {
      const config = {
        api_url: selectedProvider.value === 'custom' ? customUrl.value : (providerConfigs.value[selectedProvider.value]?.url || ''),
        api_key: apiKey.value,
        model: selectedModel.value,
      }
      const result: ChatWithToolsResult = await chatWithTools(question, config)
      assistantMsg.content = result.answer
      toolCalls.value = result.tool_calls || []
      if (result.degraded) {
        assistantMsg.content = '⚠️ ' + (result.warning || '工具调用降级') + '\n\n' + result.answer
      }
    } catch (e: any) {
      assistantMsg.content = '⚠️ 工具调用失败: ' + (e?.message || '未知错误')
    } finally {
      loading.value = false
      await nextTick()
      scrollToBottom()
    }
    return
  }

  isStreaming.value = true
  abortController = new AbortController()

  try {
    await chatStream(
      {
        question,
        provider: selectedProvider.value,
        model: selectedModel.value,
        api_key: apiKey.value,
        api_url: selectedProvider.value === 'custom' ? customUrl.value : '',
        conversation_id: activeConversationId.value,
      },
      {
        onDelta: (delta: string) => {
          assistantMsg.content += delta
          scrollToBottom()
        },
        onDone: (messageId: number, tokens: number, sources?: SearchResult[]) => {
          if (messageId > 0) assistantMsg.id = messageId
          if (tokens > 0) assistantMsg.tokens = tokens
          // P0-3.2 引用溯源：保存检索来源供下方渲染
          if (sources && sources.length > 0) assistantMsg.sources = sources
        },
        onError: (error: string) => {
          streamError.value = error
          console.error('Stream error:', error)
        },
      },
      abortController.signal
    )

    // 如果有错误且内容为空，标记为错误消息
    if (streamError.value && !assistantMsg.content) {
      assistantMsg.content = '⚠️ ' + streamError.value
    }

    // 更新会话标题（首轮）
    const conv = conversations.value.find(c => c.id === activeConversationId.value)
    if (conv && (conv.title === '新对话' || !conv.title)) {
      conv.title = question.length > 20 ? question.substring(0, 20) : question
    }
    await loadConversations()
    await nextTick()
    scrollToBottom()
  } catch (e) {
    console.error('AI chat failed:', e)
    streamError.value = '请求失败，请检查后端服务。'
    if (!assistantMsg.content) {
      assistantMsg.content = '⚠️ 请求失败，请检查后端服务。'
    }
  } finally {
    loading.value = false
    isStreaming.value = false
    abortController = null
  }
}

// 取消当前流式请求
function cancelStream() {
  if (abortController) {
    abortController.abort()
    streamError.value = '已取消'
  }
}

// 重试最后一条消息
function retryLast() {
  // 移除最后一条 assistant 消息（可能是错误或不完整的）
  if (messages.value.length > 0 && messages.value[messages.value.length - 1].role === 'assistant') {
    messages.value.pop()
  }
  streamError.value = ''
  if (lastQuestion) {
    input.value = lastQuestion
    sendMessage()
  }
}

function scrollToBottom() {
  const el = document.querySelector('.ai-messages')
  if (el) el.scrollTop = el.scrollHeight
}

// P0-3.2 引用溯源：点击来源笔记，通知父组件打开
function openSource(src: SearchResult) {
  emit('selectNote', { id: src.id, title: src.title, folder: src.folder || '' } as Note)
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
                <option v-if="getModels().length === 0 && selectedModel" :value="selectedModel">{{ selectedModel }}</option>
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
              <div v-if="selectedProvider === 'custom'" class="setting-item">
                <label>Model:</label>
                <input v-model="selectedModel" type="text" placeholder="例如 glm-4-flash / qwen-turbo" class="setting-input" />
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
                    <div class="msg-content">
                      {{ msg.content }}<span v-if="isStreaming && msg.role === 'assistant' && messages.length > 0 && msg.id === messages[messages.length - 1].id" class="stream-cursor"></span>
                    </div>
                    <!-- P0-3.2 引用溯源：来源笔记列表（点击跳转） -->
                    <div v-if="msg.role === 'assistant' && msg.sources && msg.sources.length > 0" class="msg-sources">
                      <div class="sources-title">📎 参考来源 ({{ msg.sources.length }})</div>
                      <div class="sources-list">
                        <button
                          v-for="(src, i) in msg.sources"
                          :key="src.id"
                          class="source-chip"
                          :title="src.content_highlight ? src.content_highlight.replace(/>>>|<<</g, '') : src.title"
                          @click="openSource(src)"
                        >
                          <span class="source-idx">[{{ i + 1 }}]</span>
                          {{ src.title }}
                        </button>
                      </div>
                    </div>
                    <div class="msg-time">{{ relativeTime(msg.created_at) }}</div>
                  </div>
                  <div v-if="loading && !isStreaming" class="ai-message assistant loading">
                    <div class="msg-content">{{ useTools ? '工具调用中...' : '思考中...' }}</div>
                  </div>
                  <!-- P2 工具调用过程展示 -->
                  <div v-if="toolCalls.length" class="ai-tool-calls">
                    <div class="tool-calls-title">🔧 工具调用 ({{ toolCalls.length }})</div>
                    <div v-for="(tc, i) in toolCalls" :key="i" class="tool-call-item">
                      <span class="tool-call-name">{{ tc.name }}</span>
                      <span class="tool-call-status" :class="tc.status">{{ tc.status === 'success' ? '✓' : '✗' }}</span>
                      <div class="tool-call-args">参数: {{ tc.args }}</div>
                      <div v-if="tc.status === 'error'" class="tool-call-error">{{ tc.result }}</div>
                    </div>
                  </div>
                </div>

                <!-- 错误提示 + 重试 -->
                <div v-if="streamError" class="stream-error-bar">
                  <span class="stream-error-text">⚠️ {{ streamError }}</span>
                  <button class="btn-retry" @click="retryLast">重试</button>
                </div>

                <!-- 输入区 -->
                <div class="ai-input-area">
                  <div class="ai-tool-bar">
                    <label class="tool-toggle" :class="{ active: useTools }">
                      <input type="checkbox" v-model="useTools" />
                      <span>🔧 工具模式</span>
                    </label>
                    <span v-if="useTools" class="tool-hint">搜索笔记 / 获取笔记内容</span>
                  </div>
                  <textarea
                    ref="inputRef"
                    v-model="input"
                    class="ai-input"
                    placeholder="输入问题... (Enter 发送, Shift+Enter 换行)"
                    rows="2"
                    @keydown="onKeydown"
                  />
                  <button v-if="isStreaming" class="btn-cancel" @click="cancelStream">
                    取消
                  </button>
                  <button v-else class="btn-send" @click="sendMessage" :disabled="!input.trim() || loading">
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

/* ─── P0-3.2 引用溯源：来源列表 ─── */
.msg-sources {
  margin: 6px 40px 0 0;
  padding: 8px 12px;
  background: var(--bg-hover);
  border-left: 3px solid var(--accent);
  border-radius: var(--radius-sm);
}
.sources-title {
  font-size: 11px;
  font-weight: 600;
  color: var(--text-muted);
  margin-bottom: 6px;
}
.sources-list {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}
.source-chip {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  max-width: 100%;
  padding: 3px 10px;
  border: 1px solid var(--border);
  border-radius: 999px;
  background: var(--bg-primary);
  color: var(--text-secondary);
  font-size: 12px;
  cursor: pointer;
  transition: all var(--duration-fast);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.source-chip:hover {
  border-color: var(--accent);
  color: var(--accent);
  background: var(--accent-glow);
}
.source-idx {
  color: var(--accent);
  font-weight: 600;
  flex-shrink: 0;
}

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
.stream-cursor {
  display: inline-block;
  width: 8px;
  height: 16px;
  background: var(--accent, #4a90d9);
  margin-left: 2px;
  vertical-align: text-bottom;
  animation: blink 1s step-end infinite;
}
@keyframes blink {
  0%, 100% { opacity: 1; }
  50% { opacity: 0; }
}
.stream-error-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 12px;
  background: rgba(220, 53, 69, 0.1);
  border: 1px solid rgba(220, 53, 69, 0.3);
  border-radius: var(--radius, 6px);
  margin-bottom: 8px;
}
.stream-error-text {
  color: #dc3545;
  font-size: 13px;
}
.btn-retry {
  padding: 4px 12px;
  background: #dc3545;
  color: white;
  border: none;
  border-radius: 4px;
  cursor: pointer;
  font-size: 12px;
}
.btn-retry:hover { background: #c82333; }
.btn-cancel {
  padding: 8px 16px;
  background: #6c757d;
  color: white;
  border: none;
  border-radius: var(--radius, 6px);
  cursor: pointer;
  font-size: 14px;
  white-space: nowrap;
}
.btn-cancel:hover { background: #5a6268; }

/* P2 工具模式 */
.ai-tool-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 4px 0;
}
.tool-toggle {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--text-muted);
  cursor: pointer;
  user-select: none;
}
.tool-toggle.active {
  color: var(--accent);
}
.tool-toggle input {
  cursor: pointer;
}
.tool-hint {
  font-size: 11px;
  color: var(--text-faint);
}
.ai-tool-calls {
  margin: 8px 0;
  padding: 10px;
  background: var(--bg-hover);
  border-radius: var(--radius-sm);
  border-left: 3px solid var(--accent);
}
.tool-calls-title {
  font-size: 12px;
  font-weight: 600;
  color: var(--text-secondary);
  margin-bottom: 6px;
}
.tool-call-item {
  font-size: 12px;
  padding: 4px 0;
  border-bottom: 1px solid var(--border);
}
.tool-call-item:last-child {
  border-bottom: none;
}
.tool-call-name {
  font-weight: 600;
  color: var(--accent);
}
.tool-call-status {
  margin-left: 8px;
  font-size: 11px;
}
.tool-call-status.success { color: #9ece6a; }
.tool-call-status.error { color: #f7768e; }
.tool-call-args {
  color: var(--text-muted);
  font-size: 11px;
  margin-top: 2px;
  word-break: break-all;
}
.tool-call-error {
  color: #f7768e;
  font-size: 11px;
  margin-top: 2px;
}
</style>
