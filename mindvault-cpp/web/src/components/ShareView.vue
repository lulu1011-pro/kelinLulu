<script setup lang="ts">
import { computed, ref, onMounted } from 'vue'
import { marked } from 'marked'
import { shareApi } from '../api'

interface SharedNote {
  id: number
  title: string
  content: string
  folder: string
  created_at: string
  updated_at: string
}

const note = ref<SharedNote | null>(null)
const author = ref('')
const loading = ref(true)
const error = ref('')
const htmlContent = ref('')
const shareCode = ref('')
const importing = ref(false)
const importMessage = ref('')
const importError = ref('')
const isLoggedIn = computed(() => !!localStorage.getItem('token'))

onMounted(async () => {
  const path = window.location.pathname
  const code = path.split('/share/')[1]
  shareCode.value = code || ''

  if (!code) {
    error.value = '无效的分享链接'
    loading.value = false
    return
  }

  try {
    const res = await fetch(`/api/share/${code}`)
    const data = await res.json()

    if (data.ok) {
      note.value = data.data.note
      author.value = data.data.author
      
      // 渲染 Markdown
      if (note.value?.content) {
        htmlContent.value = marked(note.value.content) as string
      }
    } else {
      error.value = data.error?.message || '分享链接不存在或已过期'
    }
  } catch (e) {
    error.value = '网络错误，请重试'
  } finally {
    loading.value = false
  }
})

async function importToMyVault() {
  if (!shareCode.value) return
  if (!isLoggedIn.value) {
    importError.value = '请先登录后再导入到我的知识库'
    importMessage.value = ''
    return
  }

  importing.value = true
  importError.value = ''
  importMessage.value = ''

  try {
    const imported = await shareApi.importNote(shareCode.value)
    importMessage.value = `已导入：${imported.title}`
  } catch (e) {
    importError.value = e instanceof Error ? e.message : '导入失败，请稍后重试'
  } finally {
    importing.value = false
  }
}

function formatDate(dateStr: string): string {
  if (!dateStr) return ''
  const d = new Date(dateStr.replace(' ', 'T'))
  return d.toLocaleDateString('zh-CN', {
    year: 'numeric',
    month: 'long',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit'
  })
}
</script>

<template>
  <div class="share-page">
    <!-- 加载中 -->
    <div v-if="loading" class="share-loading">
      <div class="spinner"></div>
      <p>加载中...</p>
    </div>

    <!-- 错误 -->
    <div v-else-if="error" class="share-error">
      <div class="error-icon">😕</div>
      <h2>无法访问</h2>
      <p>{{ error }}</p>
      <a href="/" class="btn-home">返回首页</a>
    </div>

    <!-- 笔记内容 -->
    <div v-else-if="note" class="share-content">
      <header class="share-header">
        <div class="share-meta">
          <span class="share-author">{{ author }} 分享的笔记</span>
          <span class="share-date">{{ formatDate(note.updated_at) }}</span>
        </div>
        <h1 class="share-title">{{ note.title }}</h1>
      </header>

      <article class="share-body" v-html="htmlContent"></article>

      <footer class="share-footer">
        <div class="share-import">
          <button class="btn-import" @click="importToMyVault" :disabled="importing">
            {{ importing ? '导入中...' : '导入到我的知识库' }}
          </button>
          <p v-if="!isLoggedIn" class="share-hint">当前未登录，登录后可导入到自己的知识库</p>
          <p v-if="importMessage" class="share-success">{{ importMessage }}</p>
          <p v-if="importError" class="share-error-text">{{ importError }}</p>
        </div>
        <p>由 MindVault 生成</p>
        <a href="/" class="btn-home">使用 MindVault 创建自己的知识库</a>
      </footer>
    </div>
  </div>
</template>

<style>
.share-page {
  min-height: 100vh;
  background: var(--bg-primary);
  color: var(--text-primary);
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
}

.share-loading {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  gap: 16px;
}

.spinner {
  width: 40px;
  height: 40px;
  border: 3px solid var(--border);
  border-top-color: var(--accent);
  border-radius: 50%;
  animation: spin 1s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

.share-loading p {
  color: var(--text-muted);
  font-size: 14px;
}

.share-error {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  gap: 16px;
  padding: 20px;
  text-align: center;
}

.error-icon {
  font-size: 48px;
}

.share-error h2 {
  font-size: 24px;
  color: var(--text-primary);
  margin: 0;
}

.share-error p {
  color: var(--text-muted);
  font-size: 14px;
  max-width: 400px;
}

.btn-home {
  display: inline-block;
  padding: 10px 24px;
  background: var(--accent);
  color: #fff;
  border-radius: var(--radius-sm);
  text-decoration: none;
  font-size: 14px;
  transition: background var(--duration-fast);
}

.btn-home:hover {
  background: var(--accent-hover);
}

.share-content {
  max-width: 800px;
  margin: 0 auto;
  padding: 40px 24px;
}

.share-header {
  margin-bottom: 32px;
  padding-bottom: 24px;
  border-bottom: 1px solid var(--border);
}

.share-meta {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-bottom: 12px;
}

.share-author {
  font-size: 13px;
  color: var(--text-muted);
}

.share-date {
  font-size: 13px;
  color: var(--text-faint);
}

.share-title {
  font-size: 32px;
  font-weight: 700;
  color: var(--text-primary);
  margin: 0;
  line-height: 1.3;
}

.share-body {
  font-size: 16px;
  line-height: 1.8;
  color: var(--text-secondary);
}

.share-body h1 {
  font-size: 28px;
  color: var(--accent);
  margin: 32px 0 16px;
  padding-bottom: 8px;
  border-bottom: 1px solid var(--border);
}

.share-body h2 {
  font-size: 24px;
  color: var(--purple);
  margin: 28px 0 12px;
}

.share-body h3 {
  font-size: 20px;
  color: var(--green);
  margin: 24px 0 8px;
}

.share-body p {
  margin: 12px 0;
}

.share-body code {
  background: var(--bg-secondary);
  padding: 2px 6px;
  border-radius: 4px;
  font-family: 'JetBrains Mono', Consolas, monospace;
  font-size: 14px;
  color: var(--green);
}

.share-body pre {
  background: var(--bg-secondary);
  padding: 16px;
  border-radius: var(--radius-md);
  overflow-x: auto;
  border: 1px solid var(--border);
  margin: 16px 0;
}

.share-body pre code {
  background: transparent;
  padding: 0;
  color: var(--text-primary);
}

.share-body blockquote {
  border-left: 3px solid var(--accent);
  margin: 16px 0;
  padding: 8px 16px;
  color: var(--text-muted);
  background: var(--accent-subtle);
  border-radius: 0 var(--radius-sm) var(--radius-sm) 0;
}

.share-body ul, .share-body ol {
  padding-left: 24px;
  margin: 12px 0;
}

.share-body li {
  margin: 6px 0;
}

.share-body a {
  color: var(--accent);
  text-decoration: none;
}

.share-body a:hover {
  text-decoration: underline;
}

.share-body table {
  border-collapse: collapse;
  width: 100%;
  margin: 16px 0;
}

.share-body th, .share-body td {
  border: 1px solid var(--border);
  padding: 10px 14px;
  text-align: left;
}

.share-body th {
  background: var(--bg-secondary);
  color: var(--text-primary);
  font-weight: 600;
}

.share-body img {
  max-width: 100%;
  border-radius: var(--radius-md);
}

.share-body hr {
  border: none;
  border-top: 1px solid var(--border);
  margin: 24px 0;
}

.share-footer {
  margin-top: 48px;
  padding-top: 24px;
  border-top: 1px solid var(--border);
  text-align: center;
}

.share-import {
  margin-bottom: 20px;
}

.btn-import {
  display: inline-block;
  padding: 10px 24px;
  border: 1px solid var(--accent);
  background: var(--accent);
  color: #fff;
  border-radius: var(--radius-sm);
  font-size: 14px;
  cursor: pointer;
  transition: background var(--duration-fast);
}

.btn-import:hover:not(:disabled) {
  background: var(--accent-hover);
}

.btn-import:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.share-hint,
.share-success,
.share-error-text {
  margin: 10px 0 0;
  font-size: 13px;
}

.share-hint {
  color: var(--text-muted);
}

.share-success {
  color: var(--green);
}

.share-error-text {
  color: var(--red);
}

.share-footer p {
  font-size: 13px;
  color: var(--text-faint);
  margin: 0 0 16px;
}
</style>
