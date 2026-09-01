using Microsoft.Data.Sqlite;
using System.Globalization;
using System.Text.Json;

namespace KNSoft.ZPigeon.Agent;

public sealed class AgentStore
{
    private const int SchemaVersion = 1;
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private readonly string connectionString;
    private readonly ISecretProtector protector;
    private readonly Lock sync = new();

    public AgentStore(string path, ISecretProtector protector)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ArgumentNullException.ThrowIfNull(protector);
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        connectionString = new SqliteConnectionStringBuilder
        {
            DataSource = path,
            Mode = SqliteOpenMode.ReadWriteCreate,
            Cache = SqliteCacheMode.Shared,
            ForeignKeys = true
        }.ToString();
        this.protector = protector;
        Initialize();
    }

    public ModelConfiguration[] GetModels()
    {
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = "SELECT * FROM Model ORDER BY Name COLLATE NOCASE, Id";
            using var reader = command.ExecuteReader();
            var result = new List<ModelConfiguration>();
            while (reader.Read()) result.Add(ReadModel(reader, false));
            return [.. result];
        }
    }

    public ModelConfiguration GetModel(Guid id, bool includeCredential)
    {
        ValidateId(id, nameof(id));
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = "SELECT * FROM Model WHERE Id = $id";
            command.Parameters.AddWithValue("$id", id.ToString("D"));
            using var reader = command.ExecuteReader();
            return reader.Read() ? ReadModel(reader, includeCredential) :
                throw new KeyNotFoundException("The model configuration does not exist.");
        }
    }

    public ModelConfiguration SaveModel(ModelConfiguration value, bool create)
    {
        ArgumentNullException.ThrowIfNull(value);
        ValidateId(value.Id, nameof(value));
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = create ?
                """
                INSERT INTO Model
                    (Id, Name, Provider, Protocol, BaseUrl, Authentication, Credential, ModelId,
                     ContextWindow, MaximumOutputTokens, Reasoning, RequestTimeoutSeconds, AdvancedJson)
                VALUES
                    ($id, $name, $provider, $protocol, $baseUrl, $authentication, $credential, $modelId,
                     $contextWindow, $maximumOutputTokens, $reasoning, $requestTimeoutSeconds, $advancedJson)
                """ :
                """
                UPDATE Model SET
                    Name = $name, Provider = $provider, Protocol = $protocol, BaseUrl = $baseUrl,
                    Authentication = $authentication, Credential = $credential, ModelId = $modelId,
                    ContextWindow = $contextWindow, MaximumOutputTokens = $maximumOutputTokens,
                    Reasoning = $reasoning, RequestTimeoutSeconds = $requestTimeoutSeconds,
                    AdvancedJson = $advancedJson
                WHERE Id = $id
                """;
            AddModelParameters(command, value, protector.Protect(value.Credential));
            try
            {
                if (command.ExecuteNonQuery() != 1)
                {
                    throw new KeyNotFoundException("The model configuration does not exist.");
                }
            }
            catch (SqliteException exception) when (exception.SqliteErrorCode == 19)
            {
                throw new InvalidOperationException("The model configuration already exists.", exception);
            }
            return value;
        }
    }

    public void DeleteModel(Guid id)
    {
        ValidateId(id, nameof(id));
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = "DELETE FROM Model WHERE Id = $id";
            command.Parameters.AddWithValue("$id", id.ToString("D"));
            try
            {
                if (command.ExecuteNonQuery() != 1)
                {
                    throw new KeyNotFoundException("The model configuration does not exist.");
                }
            }
            catch (SqliteException exception) when (exception.SqliteErrorCode == 19)
            {
                throw new InvalidOperationException("The model configuration is used by an agent.", exception);
            }
        }
    }

    public AgentConfiguration[] GetAgents()
    {
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = "SELECT * FROM Agent ORDER BY Name COLLATE NOCASE, Id";
            using var reader = command.ExecuteReader();
            var result = new List<AgentConfiguration>();
            while (reader.Read()) result.Add(ReadAgent(reader));
            return [.. result];
        }
    }

    public AgentConfiguration GetAgent(Guid id)
    {
        ValidateId(id, nameof(id));
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = "SELECT * FROM Agent WHERE Id = $id";
            command.Parameters.AddWithValue("$id", id.ToString("D"));
            using var reader = command.ExecuteReader();
            return reader.Read() ? ReadAgent(reader) :
                throw new KeyNotFoundException("The agent does not exist.");
        }
    }

    public AgentConfiguration SaveAgent(AgentConfiguration value, bool create)
    {
        ArgumentNullException.ThrowIfNull(value);
        ValidateId(value.Id, nameof(value));
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = create ?
                """
                INSERT INTO Agent
                    (Id, Name, ModelId, SystemPrompt, ToolNames, AgentsMd, ToolsMd, MemoryMd, Documents)
                VALUES
                    ($id, $name, $modelId, $systemPrompt, $toolNames, $agentsMd, $toolsMd, $memoryMd,
                     $documents)
                """ :
                """
                UPDATE Agent SET
                    Name = $name, ModelId = $modelId, SystemPrompt = $systemPrompt,
                    ToolNames = $toolNames, AgentsMd = $agentsMd, ToolsMd = $toolsMd,
                    MemoryMd = $memoryMd, Documents = $documents
                WHERE Id = $id
                """;
            command.Parameters.AddWithValue("$id", value.Id.ToString("D"));
            command.Parameters.AddWithValue("$name", value.Name);
            command.Parameters.AddWithValue("$modelId", value.ModelId.ToString("D"));
            command.Parameters.AddWithValue("$systemPrompt", value.SystemPrompt);
            command.Parameters.AddWithValue("$toolNames", JsonSerializer.Serialize(value.ToolNames, JsonOptions));
            command.Parameters.AddWithValue("$agentsMd", value.AgentsMd);
            command.Parameters.AddWithValue("$toolsMd", value.ToolsMd);
            command.Parameters.AddWithValue("$memoryMd", value.MemoryMd);
            command.Parameters.AddWithValue("$documents", JsonSerializer.Serialize(value.Documents, JsonOptions));
            try
            {
                if (command.ExecuteNonQuery() != 1)
                {
                    throw new KeyNotFoundException("The agent does not exist.");
                }
            }
            catch (SqliteException exception) when (exception.SqliteErrorCode == 19)
            {
                throw new InvalidOperationException("The agent references an unavailable model configuration.",
                                                    exception);
            }
            return value;
        }
    }

    public void DeleteAgent(Guid id)
    {
        ValidateId(id, nameof(id));
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = "DELETE FROM Agent WHERE Id = $id";
            command.Parameters.AddWithValue("$id", id.ToString("D"));
            try
            {
                if (command.ExecuteNonQuery() != 1) throw new KeyNotFoundException("The agent does not exist.");
            }
            catch (SqliteException exception) when (exception.SqliteErrorCode == 19)
            {
                throw new InvalidOperationException("The agent is used by a session.", exception);
            }
        }
    }

    public AgentSessionSummary[] GetSessions(string clientFingerprint, string? query)
    {
        ValidateFingerprint(clientFingerprint);
        query = string.IsNullOrWhiteSpace(query) ? null : query.Trim();
        if (query?.Length > 256) throw new ArgumentOutOfRangeException(nameof(query));
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = """
                SELECT Session.Id, Session.AgentId, Agent.Name, Session.Title,
                       Session.CreatedAt, Session.UpdatedAt
                FROM Session JOIN Agent ON Agent.Id = Session.AgentId
                WHERE Session.ClientFingerprint = $fingerprint
                  AND ($query IS NULL OR instr(lower(Session.Title), lower($query)) > 0 OR EXISTS (
                      SELECT 1 FROM SessionItem
                      WHERE SessionItem.SessionId = Session.Id
                        AND instr(lower(SessionItem.Content), lower($query)) > 0))
                ORDER BY Session.UpdatedAt DESC, Session.Id
                LIMIT 200
                """;
            command.Parameters.AddWithValue("$fingerprint", clientFingerprint);
            command.Parameters.AddWithValue("$query", (object?)query ?? DBNull.Value);
            using var reader = command.ExecuteReader();
            var result = new List<AgentSessionSummary>();
            while (reader.Read())
            {
                result.Add(new(ParseGuid(reader.GetString(0)),
                               ParseGuid(reader.GetString(1)),
                               reader.GetString(2),
                               reader.GetString(3),
                               ParseTime(reader.GetString(4)),
                               ParseTime(reader.GetString(5))));
            }
            return [.. result];
        }
    }

    public AgentSession GetSession(Guid id)
    {
        ValidateId(id, nameof(id));
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = "SELECT * FROM Session WHERE Id = $id";
            command.Parameters.AddWithValue("$id", id.ToString("D"));
            using var reader = command.ExecuteReader();
            return reader.Read() ? ReadSession(reader) :
                throw new KeyNotFoundException("The session does not exist.");
        }
    }

    public AgentSession CreateSession(Guid agentId, string clientFingerprint, string title)
    {
        ValidateId(agentId, nameof(agentId));
        ValidateFingerprint(clientFingerprint);
        ArgumentException.ThrowIfNullOrWhiteSpace(title);
        var id = Guid.NewGuid();
        var now = DateTimeOffset.UtcNow;
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = """
                INSERT INTO Session (Id, AgentId, ClientFingerprint, Title, CreatedAt, UpdatedAt)
                VALUES ($id, $agentId, $clientFingerprint, $title, $createdAt, $updatedAt)
                """;
            command.Parameters.AddWithValue("$id", id.ToString("D"));
            command.Parameters.AddWithValue("$agentId", agentId.ToString("D"));
            command.Parameters.AddWithValue("$clientFingerprint", clientFingerprint);
            command.Parameters.AddWithValue("$title", title);
            command.Parameters.AddWithValue("$createdAt", FormatTime(now));
            command.Parameters.AddWithValue("$updatedAt", FormatTime(now));
            try
            {
                command.ExecuteNonQuery();
            }
            catch (SqliteException exception) when (exception.SqliteErrorCode == 19)
            {
                throw new InvalidOperationException("The selected agent does not exist.", exception);
            }
        }
        return new(id, agentId, clientFingerprint, title, now, now);
    }

    public AgentSession RenameSession(Guid id, string title)
    {
        ValidateId(id, nameof(id));
        ArgumentException.ThrowIfNullOrWhiteSpace(title);
        var existing = GetSession(id);
        var updatedAt = DateTimeOffset.UtcNow;
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = "UPDATE Session SET Title = $title, UpdatedAt = $updatedAt WHERE Id = $id";
            command.Parameters.AddWithValue("$id", id.ToString("D"));
            command.Parameters.AddWithValue("$title", title);
            command.Parameters.AddWithValue("$updatedAt", FormatTime(updatedAt));
            if (command.ExecuteNonQuery() != 1)
            {
                throw new KeyNotFoundException("The session does not exist.");
            }
            return existing with { Title = title, UpdatedAt = updatedAt };
        }
    }

    public void DeleteSession(Guid id)
    {
        ValidateId(id, nameof(id));
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = "DELETE FROM Session WHERE Id = $id";
            command.Parameters.AddWithValue("$id", id.ToString("D"));
            if (command.ExecuteNonQuery() != 1)
            {
                throw new KeyNotFoundException("The session does not exist.");
            }
        }
    }

    public AgentSession ForkSession(Guid id, long? throughSequence)
    {
        var source = GetSession(id);
        lock (sync)
        {
            using var connection = Open();
            using var transaction = connection.BeginTransaction();
            var maximum = GetMaximumSequence(connection, transaction, id);
            var through = throughSequence ?? maximum;
            if (through < 0 || through > maximum) throw new ArgumentOutOfRangeException(nameof(throughSequence));
            var result = new AgentSession(Guid.NewGuid(),
                                          source.AgentId,
                                          source.ClientFingerprint,
                                          source.Title,
                                          DateTimeOffset.UtcNow,
                                          DateTimeOffset.UtcNow);
            using (var command = connection.CreateCommand())
            {
                command.Transaction = transaction;
                command.CommandText = """
                    INSERT INTO Session (Id, AgentId, ClientFingerprint, Title, CreatedAt, UpdatedAt)
                    VALUES ($id, $agentId, $fingerprint, $title, $createdAt, $updatedAt)
                    """;
                command.Parameters.AddWithValue("$id", result.Id.ToString("D"));
                command.Parameters.AddWithValue("$agentId", result.AgentId.ToString("D"));
                command.Parameters.AddWithValue("$fingerprint", result.ClientFingerprint);
                command.Parameters.AddWithValue("$title", result.Title);
                command.Parameters.AddWithValue("$createdAt", FormatTime(result.CreatedAt));
                command.Parameters.AddWithValue("$updatedAt", FormatTime(result.UpdatedAt));
                command.ExecuteNonQuery();
            }
            using (var command = connection.CreateCommand())
            {
                command.Transaction = transaction;
                command.CommandText = """
                    INSERT INTO SessionItem
                        (SessionId, Sequence, RunId, Step, Kind, State, Priority, Name, CallId, Content,
                         RelatedSequence, Protocol, ProtocolJson, InputTokens, CachedInputTokens, OutputTokens,
                         ReasoningTokens, TotalTokens, RawUsage, CreatedAt)
                    SELECT $target, Sequence, RunId, Step, Kind,
                           CASE WHEN State IN ($queued, $running) THEN $canceled ELSE State END,
                           0, Name, CallId, Content,
                           RelatedSequence, Protocol, ProtocolJson, InputTokens, CachedInputTokens, OutputTokens,
                           ReasoningTokens, TotalTokens, RawUsage, CreatedAt
                    FROM SessionItem
                    WHERE SessionId = $source AND Sequence <= $through
                    ORDER BY Sequence
                    """;
                command.Parameters.AddWithValue("$target", result.Id.ToString("D"));
                command.Parameters.AddWithValue("$source", id.ToString("D"));
                command.Parameters.AddWithValue("$through", through);
                command.Parameters.AddWithValue("$queued", (int)SessionItemState.Queued);
                command.Parameters.AddWithValue("$running", (int)SessionItemState.Running);
                command.Parameters.AddWithValue("$canceled", (int)SessionItemState.Canceled);
                command.ExecuteNonQuery();
            }
            transaction.Commit();
            return result;
        }
    }

    public SessionItem[] GetItems(Guid sessionId)
    {
        ValidateId(sessionId, nameof(sessionId));
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = "SELECT * FROM SessionItem WHERE SessionId = $id ORDER BY Sequence";
            command.Parameters.AddWithValue("$id", sessionId.ToString("D"));
            using var reader = command.ExecuteReader();
            var result = new List<SessionItem>();
            while (reader.Read()) result.Add(ReadItem(reader));
            return [.. result];
        }
    }

    public SessionItem AddUserMessage(Guid sessionId, string content, MessageDisposition disposition)
    {
        if (disposition is not (MessageDisposition.Queue or MessageDisposition.Steer))
        {
            throw new ArgumentOutOfRangeException(nameof(disposition));
        }
        return AddItem(sessionId,
                       Guid.NewGuid(),
                       0,
                       SessionItemKind.User,
                       SessionItemState.Queued,
                       null,
                       null,
                       content,
                       null,
                       null,
                       null,
                       TokenUsage.Empty,
                       disposition == MessageDisposition.Steer ? 1 : 0);
    }

    public SessionItem AddItem(
        Guid sessionId,
        Guid runId,
        int step,
        SessionItemKind kind,
        SessionItemState state,
        string? name,
        string? callId,
        string content,
        long? relatedSequence,
        ModelProtocol? protocol,
        string? protocolJson,
        TokenUsage usage,
        int priority = 0)
    {
        ValidateId(sessionId, nameof(sessionId));
        ValidateId(runId, nameof(runId));
        ArgumentNullException.ThrowIfNull(content);
        ArgumentNullException.ThrowIfNull(usage);
        lock (sync)
        {
            using var connection = Open();
            using var transaction = connection.BeginTransaction();
            var sequence = GetMaximumSequence(connection, transaction, sessionId) + 1;
            var createdAt = DateTimeOffset.UtcNow;
            using (var command = connection.CreateCommand())
            {
                command.Transaction = transaction;
                command.CommandText = """
                    INSERT INTO SessionItem
                        (SessionId, Sequence, RunId, Step, Kind, State, Priority, Name, CallId, Content,
                         RelatedSequence, Protocol, ProtocolJson, InputTokens, CachedInputTokens, OutputTokens,
                         ReasoningTokens, TotalTokens, RawUsage, CreatedAt)
                    VALUES
                        ($sessionId, $sequence, $runId, $step, $kind, $state, $priority, $name, $callId,
                         $content, $relatedSequence, $protocol, $protocolJson, $inputTokens,
                         $cachedInputTokens, $outputTokens, $reasoningTokens, $totalTokens, $rawUsage, $createdAt)
                    """;
                AddItemParameters(command,
                                  sessionId,
                                  sequence,
                                  runId,
                                  step,
                                  kind,
                                  state,
                                  priority,
                                  name,
                                  callId,
                                  content,
                                  relatedSequence,
                                  protocol,
                                  protocolJson,
                                  usage,
                                  createdAt);
                command.ExecuteNonQuery();
            }
            TouchSession(connection, transaction, sessionId, createdAt);
            transaction.Commit();
            return new(sessionId,
                       sequence,
                       runId,
                       step,
                       kind,
                       state,
                       name,
                       callId,
                       content,
                       relatedSequence,
                       protocol,
                       protocolJson,
                       usage.Input,
                       usage.CachedInput,
                       usage.Output,
                       usage.Reasoning,
                       usage.Total,
                       usage.RawJson,
                       createdAt);
        }
    }

    public SessionItem? GetNextQueued(Guid sessionId)
    {
        ValidateId(sessionId, nameof(sessionId));
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = """
                SELECT * FROM SessionItem
                WHERE SessionId = $id AND Kind = $kind AND State = $state
                ORDER BY Priority DESC, Sequence
                LIMIT 1
                """;
            command.Parameters.AddWithValue("$id", sessionId.ToString("D"));
            command.Parameters.AddWithValue("$kind", (int)SessionItemKind.User);
            command.Parameters.AddWithValue("$state", (int)SessionItemState.Queued);
            using var reader = command.ExecuteReader();
            return reader.Read() ? ReadItem(reader) : null;
        }
    }

    public int GetQueuedCount(Guid sessionId)
    {
        ValidateId(sessionId, nameof(sessionId));
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = """
                SELECT count(*) FROM SessionItem
                WHERE SessionId = $id AND Kind = $kind AND State = $state
                """;
            command.Parameters.AddWithValue("$id", sessionId.ToString("D"));
            command.Parameters.AddWithValue("$kind", (int)SessionItemKind.User);
            command.Parameters.AddWithValue("$state", (int)SessionItemState.Queued);
            return Convert.ToInt32(command.ExecuteScalar(), CultureInfo.InvariantCulture);
        }
    }

    public void SetItemState(Guid sessionId, long sequence, SessionItemState state)
    {
        ValidateId(sessionId, nameof(sessionId));
        if (sequence < 1) throw new ArgumentOutOfRangeException(nameof(sequence));
        lock (sync)
        {
            using var connection = Open();
            using var transaction = connection.BeginTransaction();
            using (var command = connection.CreateCommand())
            {
                command.Transaction = transaction;
                command.CommandText = """
                    UPDATE SessionItem SET State = $state
                    WHERE SessionId = $id AND Sequence = $sequence
                    """;
                command.Parameters.AddWithValue("$id", sessionId.ToString("D"));
                command.Parameters.AddWithValue("$sequence", sequence);
                command.Parameters.AddWithValue("$state", (int)state);
                if (command.ExecuteNonQuery() != 1)
                {
                    throw new KeyNotFoundException("The session item does not exist.");
                }
            }
            TouchSession(connection, transaction, sessionId, DateTimeOffset.UtcNow);
            transaction.Commit();
        }
    }

    public void CancelQueued(Guid sessionId)
    {
        ValidateId(sessionId, nameof(sessionId));
        lock (sync)
        {
            using var connection = Open();
            using var transaction = connection.BeginTransaction();
            using (var command = connection.CreateCommand())
            {
                command.Transaction = transaction;
                command.CommandText = """
                    UPDATE SessionItem SET State = $canceled
                    WHERE SessionId = $id AND Kind = $kind AND State = $queued
                    """;
                command.Parameters.AddWithValue("$id", sessionId.ToString("D"));
                command.Parameters.AddWithValue("$kind", (int)SessionItemKind.User);
                command.Parameters.AddWithValue("$queued", (int)SessionItemState.Queued);
                command.Parameters.AddWithValue("$canceled", (int)SessionItemState.Canceled);
                command.ExecuteNonQuery();
            }
            TouchSession(connection, transaction, sessionId, DateTimeOffset.UtcNow);
            transaction.Commit();
        }
    }

    public void SetInitialTitle(Guid sessionId, string content)
    {
        var title = content.Replace('\r', ' ').Replace('\n', ' ').Trim();
        if (title.Length > 80) title = title[..80];
        if (title.Length == 0) return;
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = """
                UPDATE Session SET Title = $title
                WHERE Id = $id AND NOT EXISTS (
                    SELECT 1 FROM SessionItem
                    WHERE SessionId = $id AND Kind = $user AND Sequence > 1)
                """;
            command.Parameters.AddWithValue("$id", sessionId.ToString("D"));
            command.Parameters.AddWithValue("$title", title);
            command.Parameters.AddWithValue("$user", (int)SessionItemKind.User);
            command.ExecuteNonQuery();
        }
    }

    public SessionUsage GetUsage(Guid sessionId, int contextWindow)
    {
        ValidateId(sessionId, nameof(sessionId));
        lock (sync)
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = """
                SELECT coalesce(sum(InputTokens), 0), coalesce(sum(CachedInputTokens), 0),
                       coalesce(sum(OutputTokens), 0), coalesce(sum(ReasoningTokens), 0),
                       coalesce(sum(TotalTokens), 0),
                       (SELECT InputTokens FROM SessionItem
                        WHERE SessionId = $id AND InputTokens IS NOT NULL
                        ORDER BY Sequence DESC LIMIT 1)
                FROM SessionItem WHERE SessionId = $id
                """;
            command.Parameters.AddWithValue("$id", sessionId.ToString("D"));
            using var reader = command.ExecuteReader();
            reader.Read();
            return new(reader.GetInt64(0),
                       reader.GetInt64(1),
                       reader.GetInt64(2),
                       reader.GetInt64(3),
                       reader.GetInt64(4),
                       reader.IsDBNull(5) ? null : reader.GetInt64(5),
                       contextWindow);
        }
    }

    private void Initialize()
    {
        lock (sync)
        {
            using var connection = Open();
            using (var command = connection.CreateCommand())
            {
                command.CommandText = "PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL;";
                command.ExecuteNonQuery();
            }
            using (var command = connection.CreateCommand())
            {
                command.CommandText = "PRAGMA user_version";
                var version = Convert.ToInt32(command.ExecuteScalar(), CultureInfo.InvariantCulture);
                if (version is not (0 or SchemaVersion))
                {
                    throw new InvalidDataException("The agent database schema is unsupported.");
                }
            }
            using (var command = connection.CreateCommand())
            {
                command.CommandText = """
                    CREATE TABLE IF NOT EXISTS Model (
                        Id TEXT PRIMARY KEY,
                        Name TEXT NOT NULL,
                        Provider TEXT NOT NULL,
                        Protocol INTEGER NOT NULL,
                        BaseUrl TEXT NOT NULL,
                        Authentication INTEGER NOT NULL,
                        Credential TEXT NOT NULL,
                        ModelId TEXT NOT NULL,
                        ContextWindow INTEGER NOT NULL,
                        MaximumOutputTokens INTEGER NOT NULL,
                        Reasoning INTEGER NOT NULL,
                        RequestTimeoutSeconds INTEGER NOT NULL,
                        AdvancedJson TEXT NOT NULL
                    );
                    CREATE TABLE IF NOT EXISTS Agent (
                        Id TEXT PRIMARY KEY,
                        Name TEXT NOT NULL,
                        ModelId TEXT NOT NULL REFERENCES Model(Id) ON DELETE RESTRICT,
                        SystemPrompt TEXT NOT NULL,
                        ToolNames TEXT NOT NULL,
                        AgentsMd TEXT NOT NULL,
                        ToolsMd TEXT NOT NULL,
                        MemoryMd TEXT NOT NULL,
                        Documents TEXT NOT NULL
                    );
                    CREATE TABLE IF NOT EXISTS Session (
                        Id TEXT PRIMARY KEY,
                        AgentId TEXT NOT NULL REFERENCES Agent(Id) ON DELETE RESTRICT,
                        ClientFingerprint TEXT NOT NULL,
                        Title TEXT NOT NULL,
                        CreatedAt TEXT NOT NULL,
                        UpdatedAt TEXT NOT NULL
                    );
                    CREATE INDEX IF NOT EXISTS IX_Session_Client_Updated
                        ON Session(ClientFingerprint, UpdatedAt DESC);
                    CREATE TABLE IF NOT EXISTS SessionItem (
                        SessionId TEXT NOT NULL REFERENCES Session(Id) ON DELETE CASCADE,
                        Sequence INTEGER NOT NULL,
                        RunId TEXT NOT NULL,
                        Step INTEGER NOT NULL,
                        Kind INTEGER NOT NULL,
                        State INTEGER NOT NULL,
                        Priority INTEGER NOT NULL,
                        Name TEXT,
                        CallId TEXT,
                        Content TEXT NOT NULL,
                        RelatedSequence INTEGER,
                        Protocol INTEGER,
                        ProtocolJson TEXT,
                        InputTokens INTEGER,
                        CachedInputTokens INTEGER,
                        OutputTokens INTEGER,
                        ReasoningTokens INTEGER,
                        TotalTokens INTEGER,
                        RawUsage TEXT,
                        CreatedAt TEXT NOT NULL,
                        PRIMARY KEY (SessionId, Sequence)
                    );
                    PRAGMA user_version = 1;
                    """;
                command.ExecuteNonQuery();
            }
            using (var command = connection.CreateCommand())
            {
                command.CommandText = """
                    UPDATE SessionItem SET State = $canceled
                    WHERE State IN ($queued, $running)
                    """;
                command.Parameters.AddWithValue("$canceled", (int)SessionItemState.Canceled);
                command.Parameters.AddWithValue("$queued", (int)SessionItemState.Queued);
                command.Parameters.AddWithValue("$running", (int)SessionItemState.Running);
                command.ExecuteNonQuery();
            }
        }
    }

    private SqliteConnection Open()
    {
        var connection = new SqliteConnection(connectionString);
        connection.Open();
        using var command = connection.CreateCommand();
        command.CommandText = "PRAGMA busy_timeout = 5000";
        command.ExecuteNonQuery();
        return connection;
    }

    private static void AddModelParameters(SqliteCommand command, ModelConfiguration value, string credential)
    {
        command.Parameters.AddWithValue("$id", value.Id.ToString("D"));
        command.Parameters.AddWithValue("$name", value.Name);
        command.Parameters.AddWithValue("$provider", value.Provider);
        command.Parameters.AddWithValue("$protocol", (int)value.Protocol);
        command.Parameters.AddWithValue("$baseUrl", value.BaseUrl.AbsoluteUri);
        command.Parameters.AddWithValue("$authentication", (int)value.Authentication);
        command.Parameters.AddWithValue("$credential", credential);
        command.Parameters.AddWithValue("$modelId", value.ModelId);
        command.Parameters.AddWithValue("$contextWindow", value.ContextWindow);
        command.Parameters.AddWithValue("$maximumOutputTokens", value.MaximumOutputTokens);
        command.Parameters.AddWithValue("$reasoning", (int)value.Reasoning);
        command.Parameters.AddWithValue("$requestTimeoutSeconds", value.RequestTimeoutSeconds);
        command.Parameters.AddWithValue("$advancedJson", value.AdvancedJson);
    }

    private static void AddItemParameters(
        SqliteCommand command,
        Guid sessionId,
        long sequence,
        Guid runId,
        int step,
        SessionItemKind kind,
        SessionItemState state,
        int priority,
        string? name,
        string? callId,
        string content,
        long? relatedSequence,
        ModelProtocol? protocol,
        string? protocolJson,
        TokenUsage usage,
        DateTimeOffset createdAt)
    {
        command.Parameters.AddWithValue("$sessionId", sessionId.ToString("D"));
        command.Parameters.AddWithValue("$sequence", sequence);
        command.Parameters.AddWithValue("$runId", runId.ToString("D"));
        command.Parameters.AddWithValue("$step", step);
        command.Parameters.AddWithValue("$kind", (int)kind);
        command.Parameters.AddWithValue("$state", (int)state);
        command.Parameters.AddWithValue("$priority", priority);
        command.Parameters.AddWithValue("$name", (object?)name ?? DBNull.Value);
        command.Parameters.AddWithValue("$callId", (object?)callId ?? DBNull.Value);
        command.Parameters.AddWithValue("$content", content);
        command.Parameters.AddWithValue("$relatedSequence", (object?)relatedSequence ?? DBNull.Value);
        command.Parameters.AddWithValue("$protocol", protocol.HasValue ? (int)protocol.Value : DBNull.Value);
        command.Parameters.AddWithValue("$protocolJson", (object?)protocolJson ?? DBNull.Value);
        command.Parameters.AddWithValue("$inputTokens", (object?)usage.Input ?? DBNull.Value);
        command.Parameters.AddWithValue("$cachedInputTokens", (object?)usage.CachedInput ?? DBNull.Value);
        command.Parameters.AddWithValue("$outputTokens", (object?)usage.Output ?? DBNull.Value);
        command.Parameters.AddWithValue("$reasoningTokens", (object?)usage.Reasoning ?? DBNull.Value);
        command.Parameters.AddWithValue("$totalTokens", (object?)usage.Total ?? DBNull.Value);
        command.Parameters.AddWithValue("$rawUsage", (object?)usage.RawJson ?? DBNull.Value);
        command.Parameters.AddWithValue("$createdAt", FormatTime(createdAt));
    }

    private ModelConfiguration ReadModel(SqliteDataReader reader, bool includeCredential) =>
        new(ParseGuid(reader["Id"].ToString()!),
            reader["Name"].ToString()!,
            reader["Provider"].ToString()!,
            (ModelProtocol)Convert.ToInt32(reader["Protocol"], CultureInfo.InvariantCulture),
            new Uri(reader["BaseUrl"].ToString()!, UriKind.Absolute),
            (ModelAuthentication)Convert.ToInt32(reader["Authentication"], CultureInfo.InvariantCulture),
            includeCredential ? protector.Unprotect(reader["Credential"].ToString()!) : string.Empty,
            reader["ModelId"].ToString()!,
            Convert.ToInt32(reader["ContextWindow"], CultureInfo.InvariantCulture),
            Convert.ToInt32(reader["MaximumOutputTokens"], CultureInfo.InvariantCulture),
            (ReasoningEffort)Convert.ToInt32(reader["Reasoning"], CultureInfo.InvariantCulture),
            Convert.ToInt32(reader["RequestTimeoutSeconds"], CultureInfo.InvariantCulture),
            reader["AdvancedJson"].ToString()!);

    private static AgentConfiguration ReadAgent(SqliteDataReader reader) =>
        new(ParseGuid(reader["Id"].ToString()!),
            reader["Name"].ToString()!,
            ParseGuid(reader["ModelId"].ToString()!),
            reader["SystemPrompt"].ToString()!,
            JsonSerializer.Deserialize<string[]>(reader["ToolNames"].ToString()!, JsonOptions) ?? [],
            reader["AgentsMd"].ToString()!,
            reader["ToolsMd"].ToString()!,
            reader["MemoryMd"].ToString()!,
            JsonSerializer.Deserialize<AgentDocument[]>(reader["Documents"].ToString()!, JsonOptions) ?? []);

    private static AgentSession ReadSession(SqliteDataReader reader) =>
        new(ParseGuid(reader["Id"].ToString()!),
            ParseGuid(reader["AgentId"].ToString()!),
            reader["ClientFingerprint"].ToString()!,
            reader["Title"].ToString()!,
            ParseTime(reader["CreatedAt"].ToString()!),
            ParseTime(reader["UpdatedAt"].ToString()!));

    private static SessionItem ReadItem(SqliteDataReader reader) =>
        new(ParseGuid(reader["SessionId"].ToString()!),
            Convert.ToInt64(reader["Sequence"], CultureInfo.InvariantCulture),
            ParseGuid(reader["RunId"].ToString()!),
            Convert.ToInt32(reader["Step"], CultureInfo.InvariantCulture),
            (SessionItemKind)Convert.ToInt32(reader["Kind"], CultureInfo.InvariantCulture),
            (SessionItemState)Convert.ToInt32(reader["State"], CultureInfo.InvariantCulture),
            GetNullableString(reader, "Name"),
            GetNullableString(reader, "CallId"),
            reader["Content"].ToString()!,
            GetNullableInt64(reader, "RelatedSequence"),
            reader["Protocol"] is DBNull ? null :
                (ModelProtocol)Convert.ToInt32(reader["Protocol"], CultureInfo.InvariantCulture),
            GetNullableString(reader, "ProtocolJson"),
            GetNullableInt64(reader, "InputTokens"),
            GetNullableInt64(reader, "CachedInputTokens"),
            GetNullableInt64(reader, "OutputTokens"),
            GetNullableInt64(reader, "ReasoningTokens"),
            GetNullableInt64(reader, "TotalTokens"),
            GetNullableString(reader, "RawUsage"),
            ParseTime(reader["CreatedAt"].ToString()!));

    private static long GetMaximumSequence(SqliteConnection connection, SqliteTransaction transaction, Guid sessionId)
    {
        using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = "SELECT coalesce(max(Sequence), 0) FROM SessionItem WHERE SessionId = $id";
        command.Parameters.AddWithValue("$id", sessionId.ToString("D"));
        return Convert.ToInt64(command.ExecuteScalar(), CultureInfo.InvariantCulture);
    }

    private static void TouchSession(
        SqliteConnection connection,
        SqliteTransaction transaction,
        Guid sessionId,
        DateTimeOffset updatedAt)
    {
        using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = "UPDATE Session SET UpdatedAt = $updatedAt WHERE Id = $id";
        command.Parameters.AddWithValue("$id", sessionId.ToString("D"));
        command.Parameters.AddWithValue("$updatedAt", FormatTime(updatedAt));
        if (command.ExecuteNonQuery() != 1) throw new KeyNotFoundException("The session does not exist.");
    }

    private static string? GetNullableString(SqliteDataReader reader, string name) =>
        reader[name] is DBNull ? null : reader[name].ToString();

    private static long? GetNullableInt64(SqliteDataReader reader, string name) =>
        reader[name] is DBNull ? null : Convert.ToInt64(reader[name], CultureInfo.InvariantCulture);

    private static Guid ParseGuid(string value) => Guid.ParseExact(value, "D");

    private static string FormatTime(DateTimeOffset value) => value.ToString("O", CultureInfo.InvariantCulture);

    private static DateTimeOffset ParseTime(string value) =>
        DateTimeOffset.ParseExact(value, "O", CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind);

    private static void ValidateId(Guid id, string name)
    {
        if (id == Guid.Empty) throw new ArgumentOutOfRangeException(name);
    }

    private static void ValidateFingerprint(string value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(value);
        if (value.Length > 256 || value.Contains('\0')) throw new ArgumentException(null, nameof(value));
    }
}
