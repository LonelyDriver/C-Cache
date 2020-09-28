    #pragma once
    #include <string>
    #include <mutex>
    #include <memory>
    #include <vector>
    #include <atomic>
    #include <map>
    #include <wcon/util/time.h>
    #include <wcon/logging.h>

    ENABLE_LOGGING(logger);

    namespace wcon{
        namespace util{

            template<typename Key>
            class CacheAlgorithm{
            public:
                virtual ~CacheAlgorithm(){

                }
                virtual void Insert(const Key& key) = 0;
                virtual void Remove(const Key& key) = 0;
                virtual Key& Replace() = 0;
                virtual void Increment(const Key& key) = 0;
            };

            template<typename Key>
            class LFUCache : public CacheAlgorithm<Key>{
                // muss als typename angegeben werden, weil der compiler das sonst nicht richtig auflösen kann 
                // https://stackoverflow.com/questions/610245/where-and-why-do-i-have-to-put-the-template-and-typename-keywords
                using frequence_element = typename std::multimap<std::size_t, Key>::iterator;
            public:
                LFUCache(size_t decay_border=100) : m_key_frequence(), m_key_element(), m_decay_counter(0), m_decay_border(decay_border){
                    logger::Trace(STREAM("Decayborder: "<<m_decay_border));
                }
                void Insert(const Key& key) override{
                    if(m_key_element.find(key) == m_key_element.end()){
                        const size_t init_value = 1;
                        m_key_element[key] = m_key_frequence.emplace(init_value, key);
                        logger::Trace(STREAM("Key "<<key<<" inserted"));
                    }
                    Increment(key);
                }

                void Remove(const Key& key) override{
                    auto element = m_key_element.at(key);
                    m_key_frequence.erase(element);
                    m_key_element.erase(key);
                    logger::Trace(STREAM("Key "<<key<<" removed"));
                    ++m_decay_counter;
                }
                Key& Replace() override{
                    if(m_key_frequence.empty()){
                        throw std::out_of_range("No Key registered");
                    }
                    auto lowest_freq = m_key_frequence.begin();
                    return lowest_freq->second;
                }
                void Increment(const Key& key) override{
                    auto element = m_key_element.at(key);
                    auto updated = std::make_pair(element->first+1, element->second);
                    m_key_frequence.erase(element);
                    m_key_element[key] = m_key_frequence.emplace(updated);
                    logger::Trace(STREAM("Key "<<key<<" "<<updated.first<<" times invoked"));
                    logger::Trace(STREAM("counter: "<<m_decay_counter));
                    if(++m_decay_counter >= m_decay_border){
                        decay();
                    }
                }
            private:
                void decay(){
                    for(const auto& elem : m_key_element){
                            auto up = std::make_pair(elem.second->first/2, elem.second->second);
                            m_key_frequence.erase(elem.second);
                            m_key_element[elem.first] = m_key_frequence.emplace(up);
                        }
                        logger::Trace(STREAM("All elements have been decayed"));
                    m_decay_counter = 0;
                }
                std::multimap<std::size_t, Key> m_key_frequence;                // Anzahl der Verwendung vom jeweiligen Key
                std::unordered_map<Key, frequence_element> m_key_element;       // mapping key <-> key_frequence
                size_t m_decay_counter;
                size_t m_decay_border;                                     // nach decay_border aufrufen werden alle Aufrufzähler halbiert
            };

            template<typename Key, typename Value, typename Caching>
            class Cache{
            public:
                // den benötigten caching algorithmus in place erzeugen
                Cache(const Caching& cache_algo=Caching(), size_t max_size=50) : m_max_size(max_size), m_cache_algo(cache_algo), m_data(), m_mtx(){

                }

                void Write(const Key& k, const Value& v){
                    std::lock_guard<std::mutex> lock(m_mtx);
                    if(m_data.size() >= m_max_size){
                        refresh(k);
                    }else{
                        m_cache_algo.Insert(k);
                    }
                    m_data[k] = v;
                }

                Value& Read(const Key& k){
                    std::lock_guard<std::mutex> lock(m_mtx);
                    m_cache_algo.Increment(k);
                    return m_data.at(k);
                }
                
                bool Contains(const Key& k) const{
                    std::lock_guard<std::mutex> lock(m_mtx);
                    return (m_data.find(k) != m_data.end());
                }
                
                const std::unordered_map<Key, Value> Items(){
                    std::lock_guard<std::mutex> lock(m_mtx);
                    if(m_data.empty()){
                        throw std::runtime_error("empty cache");
                    }
                    for(const auto& item : m_data){
                        m_cache_algo.Increment(item.first);
                    }
                    return m_data;
                }

                void Delete(const Key& k){
                    std::lock_guard<std::mutex> lock(m_mtx);
                    if(m_data.find(k) != m_data.end()){
                        m_data.erase(k);
                    }
                }

            private:
                void refresh(const Key& k){
                    Key key = m_cache_algo.Replace();
                    m_cache_algo.Remove(key);
                    m_data.erase(key);
                    m_cache_algo.Insert(k);
                }
                size_t m_max_size;
                Caching m_cache_algo;

                std::unordered_map<Key, Value> m_data;
                mutable std::mutex m_mtx;   // kann dann auch in const Methoden verwendet werden
            };
        }
    }

    template<typename Key, typename Value>
    using Lfucache = wcon::util::Cache<Key,Value, wcon::util::LFUCache<Key>>;
