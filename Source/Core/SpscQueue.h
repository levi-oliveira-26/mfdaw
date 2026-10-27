#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace mf
{
/**
 * Fila circular lock-free SPSC (Single-Producer / Single-Consumer).
 *
 *  - Exatamente UMA thread chama push() (produtor: UI / message thread).
 *  - Exatamente UMA thread chama pop()  (consumidor: audio thread).
 *  - Nenhuma alocação dinâmica, nenhum mutex: seguro para tempo real.
 *
 * Os índices crescem monotonicamente (nunca são zerados). Como Capacity é
 * potência de 2, "índice & mask" mapeia para a posição do buffer e a
 * aritmética sem sinal faz o wrap-around corretamente.
 *
 * Ordenação de memória:
 *  - o produtor escreve o item e só depois publica head_ com release;
 *  - o consumidor lê head_ com acquire, então enxerga o item completo;
 *  - simetricamente para tail_ (libera o slot de volta ao produtor).
 */
template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity deve ser potência de 2");
    static_assert(std::is_trivially_copyable_v<T>,
                  "T deve ser trivialmente copiável (sem alocação / destrutores)");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "std::atomic<size_t> precisa ser lock-free nesta plataforma");

public:
    SpscQueue() = default;
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    /** Somente thread PRODUTORA. Retorna false se a fila estiver cheia. */
    [[nodiscard]] bool push(const T& item) noexcept
    {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_acquire);

        if (head - tail == Capacity)
            return false; // cheia: o chamador decide o que fazer (descartar/tentar depois)

        buffer_[head & mask] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    /** Somente thread CONSUMIDORA. Retorna false se a fila estiver vazia. */
    [[nodiscard]] bool pop(T& out) noexcept
    {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto head = head_.load(std::memory_order_acquire);

        if (tail == head)
            return false; // vazia

        out = buffer_[tail & mask];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    /** Estimativa (pode estar defasada se chamada de outra thread). */
    [[nodiscard]] std::size_t approxSize() const noexcept
    {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    static constexpr std::size_t mask = Capacity - 1;
    static constexpr std::size_t cacheLine = 64; // evita false sharing entre head_ e tail_

    alignas(cacheLine) std::atomic<std::size_t> head_ { 0 }; // escrito só pelo produtor
    alignas(cacheLine) std::atomic<std::size_t> tail_ { 0 }; // escrito só pelo consumidor
    alignas(cacheLine) std::array<T, Capacity> buffer_ {};
};

} // namespace mf
