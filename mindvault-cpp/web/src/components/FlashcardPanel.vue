<script setup lang="ts">
import { ref, watch, computed } from 'vue'
import { flashcardsApi, notesApi, type Flashcard } from '../api'

const props = defineProps<{
  noteId: number
  noteTitle: string
  visible: boolean
}>()

const emit = defineEmits<{
  close: []
}>()

const mode = ref<'manage' | 'study'>('manage')
const cards = ref<Flashcard[]>([])
const dueCards = ref<Flashcard[]>([])
const loading = ref(false)
const generating = ref(false)

// 创建闪卡
const showCreate = ref(false)
const newFront = ref('')
const newBack = ref('')

// 学习模式
const currentIndex = ref(0)
const showAnswer = ref(false)

watch(() => props.visible, (v) => {
  if (v) loadCards()
})

async function loadCards() {
  loading.value = true
  try {
    cards.value = await flashcardsApi.getByNote(props.noteId)
    dueCards.value = await flashcardsApi.getDue()
  } catch (e) {
    console.error('Failed to load flashcards:', e)
  } finally {
    loading.value = false
  }
}

// AI 生成闪卡
async function generateByAI() {
  generating.value = true
  try {
    // 获取笔记内容
    const note = await notesApi.get(props.noteId)
    const content = note.content || ''

    if (!content.trim()) {
      alert('笔记内容为空，无法生成闪卡')
      return
    }

    // 获取 AI 配置
    const provider = localStorage.getItem('ai-provider') || 'tongyi'
    const configStr = localStorage.getItem(`ai-config-${provider}`)
    let apiKey = ''
    let apiModel = localStorage.getItem('ai-model') || 'qwen-turbo'

    if (configStr) {
      try {
        const config = JSON.parse(configStr)
        apiKey = config.key || ''
        if (config.model) apiModel = config.model
      } catch (e) {
        console.error('Failed to parse AI config:', e)
      }
    }

    console.log('AI Config:', { provider, apiKey: apiKey ? '***' : 'empty', model: apiModel })

    if (!apiKey) {
      alert('请先配置 AI API Key：\n1. 点击右下角 🤖 按钮\n2. 点击 ⚙️ 按钮\n3. 输入 API Key 并保存')
      return
    }

    // 调用 AI API 生成闪卡
    const res = await fetch('/api/ai/chat', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        question: `请根据以下笔记内容生成5个闪卡（问答对）。只返回JSON数组格式，不要其他文字：[{"front": "问题", "back": "答案"}]\n\n笔记内容：\n${content.substring(0, 2000)}`,
        provider: provider,
        model: apiModel,
        api_key: apiKey,
      }),
    })

    const data = await res.json()
    console.log('AI Response:', data)

    if (data.ok) {
      // 尝试从回答中提取 JSON
      const answer = data.data.answer
      const jsonMatch = answer.match(/\[[\s\S]*?\]/)
      if (jsonMatch) {
        try {
          const flashcards = JSON.parse(jsonMatch[0])
          // 创建闪卡
          let created = 0
          for (const fc of flashcards) {
            if (fc.front && fc.back) {
              await flashcardsApi.create({
                note_id: props.noteId,
                front: fc.front,
                back: fc.back,
              })
              created++
            }
          }
          await loadCards()
          alert(`成功生成 ${created} 张闪卡！`)
        } catch (e) {
          console.error('Failed to parse flashcards JSON:', e)
          alert('AI 返回格式不正确，请重试')
        }
      } else {
        alert('AI 返回格式不正确，请重试')
      }
    } else {
      alert('AI 请求失败：' + (data.error?.message || '未知错误'))
    }
  } catch (e) {
    console.error('Failed to generate flashcards:', e)
    alert('生成失败，请检查 AI 配置')
  } finally {
    generating.value = false
  }
}

async function createCard() {
  if (!newFront.value.trim() || !newBack.value.trim()) return
  try {
    const card = await flashcardsApi.create({
      note_id: props.noteId,
      front: newFront.value,
      back: newBack.value,
    })
    cards.value.push(card)
    newFront.value = ''
    newBack.value = ''
    showCreate.value = false
  } catch (e) {
    console.error('Failed to create flashcard:', e)
  }
}

async function deleteCard(id: number) {
  try {
    await flashcardsApi.delete(id)
    cards.value = cards.value.filter(c => c.id !== id)
    dueCards.value = dueCards.value.filter(c => c.id !== id)
  } catch (e) {
    console.error('Failed to delete flashcard:', e)
  }
}

function startStudy() {
  if (dueCards.value.length === 0) return
  mode.value = 'study'
  currentIndex.value = 0
  showAnswer.value = false
}

async function reviewCard(quality: number) {
  const card = dueCards.value[currentIndex.value]
  if (!card) return

  try {
    await flashcardsApi.review(card.id, quality)
    showAnswer.value = false

    if (currentIndex.value < dueCards.value.length - 1) {
      currentIndex.value++
    } else {
      mode.value = 'manage'
      await loadCards()
    }
  } catch (e) {
    console.error('Failed to review flashcard:', e)
  }
}

const currentCard = computed(() => dueCards.value[currentIndex.value])
const progress = computed(() => `${currentIndex.value + 1}/${dueCards.value.length}`)
</script>

<template>
  <Teleport to="body">
    <Transition name="overlay">
      <div v-if="visible" class="flash-overlay" @click.self="emit('close')">
        <Transition name="modal">
          <div v-if="visible" class="flash-modal">
            <div class="flash-header">
              <span class="flash-icon">🃏</span>
              <span class="flash-title">闪卡 - {{ noteTitle }}</span>
              <div class="flash-tabs">
                <button :class="{ active: mode === 'manage' }" @click="mode = 'manage'">管理</button>
                <button :class="{ active: mode === 'study' }" @click="startStudy">
                  学习 <span v-if="dueCards.length > 0" class="due-badge">{{ dueCards.length }}</span>
                </button>
              </div>
              <button class="btn-close" @click="emit('close')">×</button>
            </div>

            <!-- 管理模式 -->
            <div v-if="mode === 'manage'" class="flash-body">
              <div class="flash-toolbar">
                <button class="btn-create" @click="showCreate = true">+ 创建闪卡</button>
                <button class="btn-ai" @click="generateByAI" :disabled="generating">
                  {{ generating ? '生成中...' : '🤖 AI 生成' }}
                </button>
                <span class="card-count">{{ cards.length }} 张闪卡</span>
              </div>

              <!-- 创建表单 -->
              <div v-if="showCreate" class="create-form">
                <div class="form-group">
                  <label>问题：</label>
                  <textarea v-model="newFront" placeholder="输入问题..." rows="2" />
                </div>
                <div class="form-group">
                  <label>答案：</label>
                  <textarea v-model="newBack" placeholder="输入答案..." rows="2" />
                </div>
                <div class="form-actions">
                  <button class="btn-cancel" @click="showCreate = false">取消</button>
                  <button class="btn-save" @click="createCard">保存</button>
                </div>
              </div>

              <!-- 闪卡列表 -->
              <div class="card-list">
                <div v-if="loading" class="loading">加载中...</div>
                <div v-else-if="cards.length === 0" class="empty">
                  暂无闪卡，点击上方按钮创建
                </div>
                <div v-for="card in cards" :key="card.id" class="card-item">
                  <div class="card-content">
                    <div class="card-front">{{ card.front }}</div>
                    <div class="card-back">{{ card.back }}</div>
                  </div>
                  <button class="btn-delete" @click="deleteCard(card.id)">×</button>
                </div>
              </div>
            </div>

            <!-- 学习模式 -->
            <div v-if="mode === 'study'" class="flash-body study-mode">
              <div v-if="dueCards.length === 0" class="empty">
                没有待复习的闪卡
              </div>
              <div v-else-if="currentCard" class="study-card">
                <div class="progress">{{ progress }}</div>
                <div class="card-display">
                  <div class="card-question">{{ currentCard.front }}</div>
                  <button v-if="!showAnswer" class="btn-show" @click="showAnswer = true">
                    显示答案
                  </button>
                  <div v-else class="card-answer">
                    <div class="answer-divider">答案</div>
                    <div class="answer-text">{{ currentCard.back }}</div>
                  </div>
                </div>
                <div v-if="showAnswer" class="review-buttons">
                  <button class="btn-review btn-1" @click="reviewCard(1)">😰 忘记</button>
                  <button class="btn-review btn-2" @click="reviewCard(2)">😟 困难</button>
                  <button class="btn-review btn-3" @click="reviewCard(3)">😐 一般</button>
                  <button class="btn-review btn-4" @click="reviewCard(4)">😊 记住</button>
                  <button class="btn-review btn-5" @click="reviewCard(5)">😎 简单</button>
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
.flash-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.flash-modal {
  width: 700px;
  max-width: 90vw;
  height: 600px;
  max-height: 80vh;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  box-shadow: var(--shadow-xl);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.flash-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 12px 16px;
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}

.flash-icon { font-size: 18px; }

.flash-title {
  flex: 1;
  font-size: 15px;
  font-weight: 600;
  color: var(--text-primary);
}

.flash-tabs {
  display: flex;
  gap: 4px;
  background: var(--bg-primary);
  border-radius: var(--radius-sm);
  padding: 2px;
}

.flash-tabs button {
  padding: 4px 12px;
  border: none;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-muted);
  font-size: 12px;
  cursor: pointer;
  transition: all var(--duration-fast);
}

.flash-tabs button.active {
  background: var(--bg-secondary);
  color: var(--text-primary);
}

.due-badge {
  background: var(--accent);
  color: #fff;
  padding: 1px 6px;
  border-radius: 10px;
  font-size: 10px;
  margin-left: 4px;
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

.flash-body {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
}

.flash-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 16px;
  gap: 8px;
}

.btn-create {
  padding: 8px 16px;
  border: 1px solid var(--accent);
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--accent);
  font-size: 13px;
  cursor: pointer;
}

.btn-create:hover {
  background: var(--accent-glow);
}

.btn-ai {
  padding: 8px 16px;
  border: 1px solid var(--purple);
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--purple);
  font-size: 13px;
  cursor: pointer;
}

.btn-ai:hover:not(:disabled) {
  background: var(--purple-glow);
}

.btn-ai:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.card-count {
  font-size: 12px;
  color: var(--text-muted);
}

.create-form {
  background: var(--bg-primary);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  padding: 16px;
  margin-bottom: 16px;
}

.form-group {
  margin-bottom: 12px;
}

.form-group label {
  display: block;
  font-size: 12px;
  color: var(--text-muted);
  margin-bottom: 4px;
}

.form-group textarea {
  width: 100%;
  padding: 8px 12px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--bg-secondary);
  color: var(--text-primary);
  font-size: 13px;
  font-family: inherit;
  resize: vertical;
}

.form-group textarea:focus {
  outline: none;
  border-color: var(--accent);
}

.form-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}

.btn-cancel, .btn-save {
  padding: 6px 16px;
  border: none;
  border-radius: var(--radius-sm);
  font-size: 12px;
  cursor: pointer;
}

.btn-cancel {
  background: var(--bg-tertiary);
  color: var(--text-secondary);
}

.btn-save {
  background: var(--accent);
  color: #fff;
}

.card-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.loading, .empty {
  text-align: center;
  color: var(--text-muted);
  font-size: 13px;
  padding: 32px 16px;
}

.card-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 16px;
  background: var(--bg-primary);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
}

.card-content {
  flex: 1;
  min-width: 0;
}

.card-front {
  font-size: 13px;
  color: var(--text-primary);
  font-weight: 500;
  margin-bottom: 4px;
}

.card-back {
  font-size: 12px;
  color: var(--text-muted);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.btn-delete {
  width: 24px;
  height: 24px;
  border: none;
  border-radius: 50%;
  background: transparent;
  color: var(--text-muted);
  font-size: 16px;
  cursor: pointer;
  flex-shrink: 0;
}

.btn-delete:hover {
  background: var(--red-glow);
  color: var(--red);
}

.study-mode {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
}

.study-card {
  width: 100%;
  max-width: 500px;
  text-align: center;
}

.progress {
  font-size: 12px;
  color: var(--text-muted);
  margin-bottom: 24px;
}

.card-display {
  background: var(--bg-primary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  padding: 32px 24px;
  margin-bottom: 24px;
}

.card-question {
  font-size: 18px;
  color: var(--text-primary);
  font-weight: 500;
  margin-bottom: 24px;
}

.btn-show {
  padding: 10px 32px;
  border: 1px solid var(--accent);
  border-radius: var(--radius-md);
  background: transparent;
  color: var(--accent);
  font-size: 14px;
  cursor: pointer;
}

.btn-show:hover {
  background: var(--accent-glow);
}

.answer-divider {
  font-size: 12px;
  color: var(--text-muted);
  margin-bottom: 12px;
  padding-top: 16px;
  border-top: 1px solid var(--border);
}

.answer-text {
  font-size: 16px;
  color: var(--text-secondary);
  line-height: 1.6;
}

.review-buttons {
  display: flex;
  gap: 8px;
  justify-content: center;
}

.btn-review {
  padding: 10px 16px;
  border: none;
  border-radius: var(--radius-md);
  font-size: 13px;
  cursor: pointer;
  transition: all var(--duration-fast);
}

.btn-1 { background: var(--red-glow); color: var(--red); }
.btn-2 { background: rgba(249, 115, 22, 0.1); color: var(--orange); }
.btn-3 { background: var(--bg-tertiary); color: var(--text-secondary); }
.btn-4 { background: rgba(34, 197, 94, 0.1); color: var(--green); }
.btn-5 { background: rgba(122, 162, 247, 0.1); color: var(--accent); }

.btn-review:hover {
  transform: scale(1.05);
}
</style>
