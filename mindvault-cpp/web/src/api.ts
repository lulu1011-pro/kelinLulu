// MindVault API Client - 封装所有后端 API 调用

const API_BASE = '/api'

function getToken(): string | null {
  return localStorage.getItem('token')
}

async function request<T>(path: string, options?: RequestInit): Promise<T> {
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
  }
  const token = getToken()
  if (token) {
    headers['Authorization'] = `Bearer ${token}`
  }

  const res = await fetch(`${API_BASE}${path}`, {
    headers,
    ...options,
  })
  const json = await res.json()
  if (!json.ok) {
    throw new Error(json.error?.message || 'API Error')
  }
  return json.data as T
}

// ─── Types ───

export interface User {
  id: number
  username: string
  nickname: string
  token?: string
}

export interface Note {
  id: number
  title: string
  content?: string
  folder: string
  is_deleted: number
  sort_order?: number
  created_at: string
  updated_at: string
}

export interface Tag {
  id: number
  name: string
  note_count?: number
}

export interface SearchResult {
  id: number
  title: string
  folder: string
  updated_at: string
  title_highlight?: string
  content_highlight?: string
}

// 高亮标记转换：>>>text<<< → <mark>text</mark>
export function highlightToHtml(text?: string): string {
  if (!text) return ''
  return text.replace(/>>>/g, '<mark>').replace(/<<</g, '</mark>')
}

// ─── Notes ───

export const notesApi = {
  list: (folder?: string) =>
    request<Note[]>(`/notes${folder ? `?folder=${encodeURIComponent(folder)}` : ''}`),

  get: (id: number) =>
    request<Note>(`/notes/${id}`),

  create: (data: { title: string; content?: string; folder?: string }) =>
    request<Note>('/notes', { method: 'POST', body: JSON.stringify(data) }),

  update: (id: number, data: { title: string; content: string; folder?: string }) =>
    request<Note>(`/notes/${id}`, { method: 'PUT', body: JSON.stringify(data) }),

  delete: (id: number) =>
    request<void>(`/notes/${id}`, { method: 'DELETE' }),

  trash: () =>
    request<Note[]>('/notes/trash'),

  restore: (id: number) =>
    request<Note>(`/notes/${id}/restore`, { method: 'POST' }),

  permanentDelete: (id: number) =>
    request<void>(`/notes/${id}/permanent`, { method: 'DELETE' }),

  emptyTrash: () =>
    request<{ deleted_count: number }>('/notes/trash/empty', { method: 'DELETE' }),

  backlinks: (id: number) =>
    request<Note[]>(`/notes/${id}/backlinks`),

  links: (id: number) =>
    request<Note[]>(`/notes/${id}/links`),

  updateOrder: (id: number, sortOrder: number) =>
    request<void>(`/notes/${id}/order`, { method: 'PUT', body: JSON.stringify({ sort_order: sortOrder }) }),

  batchUpdateOrder: (orders: { id: number; order: number }[]) =>
    request<void>('/notes/order', { method: 'PUT', body: JSON.stringify({ orders }) }),

  getVersions: (id: number) =>
    request<Version[]>(`/notes/${id}/versions`),

  getVersion: (versionId: number) =>
    request<Version>(`/versions/${versionId}`),
}

export interface Version {
  id: number
  note_id: number
  title: string
  content?: string
  created_at: string
}

// ─── AI Conversations ───

export interface Conversation {
  id: number
  title: string
  updated_at: string
  message_count: number
}

export interface ConversationDetail extends Conversation {
  messages: ChatMessage[]
}

export interface ChatMessage {
  id: number
  role: 'user' | 'assistant'
  content: string
  tokens: number
  created_at: string
}

export interface ChatRequest {
  question: string
  provider: string
  model: string
  api_key: string
  api_url?: string
  conversation_id?: number
}

export interface ChatResponse {
  answer: string
  sources: SearchResult[]
  provider: string
  model: string
  conversation_id: number
  message_id: number
}

export const conversationsApi = {
  list: () =>
    request<Conversation[]>('/ai/conversations'),

  create: (title?: string) =>
    request<Conversation>('/ai/conversations', {
      method: 'POST',
      body: JSON.stringify({ title }),
    }),

  get: (id: number) =>
    request<ConversationDetail>(`/ai/conversations/${id}`),

  delete: (id: number) =>
    request<void>(`/ai/conversations/${id}`, { method: 'DELETE' }),

  rename: (id: number, title: string) =>
    request<void>(`/ai/conversations/${id}`, {
      method: 'PUT',
      body: JSON.stringify({ title }),
    }),
}

// ─── P0-2 SSE 流式对话 ───

export interface StreamCallbacks {
  onDelta: (delta: string) => void
  onDone: (messageId: number, tokens: number) => void
  onError: (error: string) => void
}

/**
 * 流式对话：用 fetch + ReadableStream 逐块读取 SSE（EventSource 不支持 POST）
 * 支持通过 AbortSignal 中途取消
 */
export async function chatStream(
  req: ChatRequest,
  callbacks: StreamCallbacks,
  signal?: AbortSignal
): Promise<void> {
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
    'Accept': 'text/event-stream',
  }
  const token = getToken()
  if (token) headers['Authorization'] = `Bearer ${token}`

  let res: Response
  try {
    res = await fetch(`${API_BASE}/ai/chat/stream`, {
      method: 'POST',
      headers,
      body: JSON.stringify(req),
      signal,
    })
  } catch (e: any) {
    if (e.name === 'AbortError') {
      callbacks.onError('请求已取消')
    } else {
      callbacks.onError(e.message || '网络错误')
    }
    return
  }

  if (!res.ok) {
    let errMsg = `HTTP ${res.status}`
    try {
      const errJson = await res.json()
      errMsg = errJson.error?.message || errMsg
    } catch { /* ignore */ }
    callbacks.onError(errMsg)
    return
  }

  const reader = res.body?.getReader()
  if (!reader) {
    callbacks.onError('无响应体')
    return
  }

  const decoder = new TextDecoder()
  let buffer = ''

  try {
    while (true) {
      const { done, value } = await reader.read()
      if (done) break

      buffer += decoder.decode(value, { stream: true })

      // SSE 事件以空行（\n\n）分隔
      let idx: number
      while ((idx = buffer.indexOf('\n\n')) !== -1) {
        const event = buffer.substring(0, idx)
        buffer = buffer.substring(idx + 2)

        // 解析 data: {...}
        const dataMatch = event.match(/^data:\s*(.+)$/m)
        if (!dataMatch) continue

        const dataStr = dataMatch[1].trim()
        if (!dataStr || dataStr === '[DONE]') continue

        try {
          const data = JSON.parse(dataStr)
          if (data.type === 'delta') {
            callbacks.onDelta(data.content || '')
          } else if (data.type === 'done') {
            callbacks.onDone(data.message_id || 0, data.tokens || 0)
          } else if (data.type === 'error') {
            callbacks.onError(data.message || '服务端错误')
          }
        } catch {
          // 忽略解析失败的 chunk（可能是不完整的 JSON）
        }
      }
    }
  } catch (e: any) {
    if (e.name === 'AbortError') {
      callbacks.onError('请求已取消')
    } else {
      callbacks.onError(e.message || '流读取错误')
    }
  } finally {
    try { reader.releaseLock() } catch { /* ignore */ }
  }
}

// ─── Search ───

export const searchApi = {
  search: (query: string) =>
    request<SearchResult[]>(`/search?q=${encodeURIComponent(query)}`),

  byTitle: (keyword: string) =>
    request<Note[]>(`/search/title?keyword=${encodeURIComponent(keyword)}`),
}

// ─── Tags ───

export const tagsApi = {
  list: () =>
    request<Tag[]>('/tags'),

  addToNote: (noteId: number, name: string) =>
    request<void>(`/notes/${noteId}/tags`, { method: 'POST', body: JSON.stringify({ name }) }),

  removeFromNote: (noteId: number, tagName: string) =>
    request<void>(`/notes/${noteId}/tags`, { method: 'DELETE', body: JSON.stringify({ name: tagName }) }),

  getNoteTags: (noteId: number) =>
    request<Tag[]>(`/notes/${noteId}/tags`, { method: 'GET' }),

  getNotesByTag: (tagName: string) =>
    request<Note[]>(`/tags/${encodeURIComponent(tagName)}/notes`),
}

// ─── Files ───

export const filesApi = {
  exportNote: (id: number) =>
    `${API_BASE}/files/export/${id}`,

  exportHtml: (id: number) =>
    `${API_BASE}/files/export-html/${id}`,

  importFile: (data: { filename: string; content: string; folder?: string }) =>
    request<Note>('/files/import', { method: 'POST', body: JSON.stringify(data) }),

  uploadImage: async (file: File): Promise<{ url: string; filename: string }> => {
    const formData = new FormData()
    formData.append('file', file)
    const res = await fetch(`${API_BASE}/upload/image`, {
      method: 'POST',
      body: formData,
    })
    const json = await res.json()
    if (!json.ok) throw new Error(json.error?.message || 'Upload failed')
    return json.data as { url: string; filename: string }
  },

  uploadBase64: async (dataUrl: string): Promise<{ url: string; filename: string }> => {
    const token = localStorage.getItem('token')
    const headers: Record<string, string> = { 'Content-Type': 'application/json' }
    if (token) headers['Authorization'] = `Bearer ${token}`
    const res = await fetch(`${API_BASE}/upload/base64`, {
      method: 'POST',
      headers,
      body: JSON.stringify({ image: dataUrl }),
    })
    const json = await res.json()
    if (!json.ok) throw new Error(json.error?.message || 'Upload failed')
    return json.data as { url: string; filename: string }
  },
}

// ─── Flashcards ───

export interface Flashcard {
  id: number
  note_id: number
  front: string
  back: string
  ease_factor: number
  interval_days: number
  next_review: string | null
  created_at: string
  note_title?: string
}

export const flashcardsApi = {
  getByNote: (noteId: number) =>
    request<Flashcard[]>(`/notes/${noteId}/flashcards`),

  create: (data: { note_id: number; front: string; back: string }) =>
    request<Flashcard>('/flashcards', { method: 'POST', body: JSON.stringify(data) }),

  update: (id: number, data: { front: string; back: string }) =>
    request<Flashcard>(`/flashcards/${id}`, { method: 'PUT', body: JSON.stringify(data) }),

  delete: (id: number) =>
    request<void>(`/flashcards/${id}`, { method: 'DELETE' }),

  getDue: () =>
    request<Flashcard[]>('/flashcards/due'),

  review: (id: number, quality: number) =>
    request<Flashcard>(`/flashcards/${id}/review`, { method: 'POST', body: JSON.stringify({ quality }) }),
}

// ─── Auth ───

export const authApi = {
  login: (username: string, password: string) =>
    request<User>('/auth/login', { method: 'POST', body: JSON.stringify({ username, password }) }),

  register: (username: string, password: string, nickname?: string) =>
    request<User>('/auth/register', { method: 'POST', body: JSON.stringify({ username, password, nickname }) }),

  me: () =>
    request<User>('/auth/me'),
}

// ─── Collab ───

export const collabApi = {
  join: (noteId: number) =>
    request<void>(`/notes/${noteId}/join`, { method: 'POST' }),

  leave: (noteId: number) =>
    request<void>(`/notes/${noteId}/leave`, { method: 'POST' }),

  heartbeat: (noteId: number) =>
    request<void>(`/notes/${noteId}/heartbeat`, { method: 'POST' }),

  viewers: (noteId: number) =>
    request<{ id: number; username: string }[]>(`/notes/${noteId}/viewers`),
}

// ─── Share ───

export const shareApi = {
  importNote: (code: string) =>
    request<Note>(`/share/${code}/import`, { method: 'POST' }),
}

// ─── Drawings ───

export interface Drawing {
  id: number
  note_id: number
  url: string
  created_at: string
}

export const drawingsApi = {
  save: (noteId: number, imageData: string) =>
    request<{ url: string; filename: string }>('/drawings', {
      method: 'POST',
      body: JSON.stringify({ note_id: noteId, image_data: imageData }),
    }),
}
