<script setup lang="ts">
import { ref, watch } from 'vue'

interface ChatMessage {
  id: string
  role: 'user' | 'assistant'
  content: string
  sources?: { title: string; snippet: string }[]
  timestamp: number
}

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

const messages = ref<ChatMessage[]>([])
const input = ref('')
const loading = ref(false)
const showSettings = ref(false)

// AI Config - 每个提供商独立配置
const providers = ref<Provider[]>([])
const selectedProvider = ref(localStorage.getItem('ai-provider') || 'tongyi')
const selectedModel = ref('')
const ollamaAvailable = ref(false)
const ollamaModels = ref<string[]>([])

// 各提供商独立的 Key 和 URL
const providerConfigs = ref<Record<string, { key: string; url: string; model: string }>>({})

// 当前提供商的配置
const apiKey = ref('')
const customUrl = ref('')

// 加载提供商配置
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

// 保存提供商配置
function saveProviderConfig() {
  const config = { key: apiKey.value, url: customUrl.value, model: selectedModel.value }
  providerConfigs.value[selectedProvider.value] = config
  localStorage.setItem(`ai-config-${selectedProvider.value}`, JSON.stringify(config))
  localStorage.setItem('ai-provider', selectedProvider.value)
  showSettings.value = false
}

watch(() => props.visible, (v) => {
  if (v) {
    checkStatus()
    loadProviderConfig(selectedProvider.value)
  }
})

watch(selectedProvider, (v) => {
  loadProviderConfig(v)
  // 更新模型列表
  const models = getModels()
  if (models.length > 0 && !models.includes(selectedModel.value)) {
    selectedModel.value = models[0]
  }
})

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

async function sendMessage() {
  if (!input.value.trim() || loading.value) return

  const question = input.value
  input.value = ''

  messages.value.push({
    id: Date.now().toString(),
    role: 'user',
    content: question,
    timestamp: Date.now(),
  })

  loading.value = true
  try {
    const res = await fetch('/api/ai/chat', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        question,
        provider: selectedProvider.value,
        model: selectedModel.value,
        api_key: apiKey.value,
        api_url: selectedProvider.value === 'custom' ? customUrl.value : '',
      }),
    })
    const data = await res.json()
    if (data.ok) {
      messages.value.push({
        id: (Date.now() + 1).toString(),
        role: 'assistant',
        content: data.data.answer,
        sources: data.data.sources?.map((s: any) => ({
          title: s.title,
          snippet: s.content_highlight || s.title_highlight || '',
        })),
        timestamp: Date.now(),
      })
    }
  } catch (e) {
    console.error('AI chat failed:', e)
    messages.value.push({
      id: (Date.now() + 1).toString(),
      role: 'assistant',
      content: '请求失败，请检查后端服务。',
      timestamp: Date.now(),
    })
  } finally {
    loading.value = false
  }
}

function onKeydown(e: KeyboardEvent) {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault()
    sendMessage()
  }
}

function clearChat() {
  messages.value = []
}
</script>

<template>
  <Teleport to="body">
    <Transition name="overlay">
      <div v-if="visible" class="ai-overlay" @click.self="emit('close')">
        <Transition name="modal">
          <div v-if="visible" class="ai-panel">
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
              <button class="btn-clear" @click="clearChat" title="清空对话">🗑️</button>
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
                <div v-if="msg.sources && msg.sources.length > 0" class="msg-sources">
                  <div class="sources-label">参考来源：</div>
                  <div v-for="(s, i) in msg.sources" :key="i" class="source-item">
                    📄 {{ s.title }}
                  </div>
                </div>
              </div>
              <div v-if="loading" class="ai-message assistant loading">
                <div class="msg-content">思考中...</div>
              </div>
            </div>

            <div class="ai-input-area">
              <textarea
                v-model="input"
                class="ai-input"
                placeholder="输入问题..."
                rows="2"
                @keydown="onKeydown"
              />
              <button class="btn-send" @click="sendMessage" :disabled="!input.trim() || loading">
                发送
              </button>
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
  width: 600px;
  max-width: 90vw;
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

.ai-header {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 12px 16px;
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

.btn-settings, .btn-clear, .btn-close {
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

.btn-settings:hover, .btn-clear:hover, .btn-close:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

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

.ai-message { margin-bottom: 16px; }

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

.msg-sources {
  margin-top: 8px;
  padding: 8px 12px;
  background: var(--bg-tertiary);
  border-radius: var(--radius-sm);
  margin-right: 40px;
}

.sources-label {
  font-size: 11px;
  color: var(--text-muted);
  margin-bottom: 4px;
}

.source-item {
  font-size: 12px;
  color: var(--text-secondary);
  padding: 2px 0;
}

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
</style>
