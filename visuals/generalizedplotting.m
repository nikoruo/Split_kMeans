function fig = generalizedplotting(varargin)
% GENERALIZEDPLOTTING  Draw 2-D clusterings from Split_kMeans snapshot files.
%
%   generalizedplotting()
%       Asks for a data, a centroids and a partition file (as the v1 script).
%   fig = generalizedplotting(data, snapshots, Name, Value, ...)
%       Draws one panel per snapshot, all with the same axis limits.
%
%   data       Data file (two whitespace-separated columns) or an N-by-2 matrix.
%   snapshots  Snapshot, or cell array of snapshots, each either a prefix such as
%              'run/s4/SKM_Local_run001_iter0007_split' (reads <prefix>_centroids.txt
%              and <prefix>_partitions.txt) or a cell {centroidsFile, partitionsFile}.
%
%   Options (Name, Value):
%   'Titles'       Cell array of panel titles, drawn as plain text (default: none).
%   'GroundTruth'  Ground-truth centroids file, drawn as blue filled circles.
%   'Highlight'    true: compare consecutive snapshots and fill in grey the cluster
%                  split next, its two halves after the split, and, after a step that
%                  keeps K, the two halves and every cluster that gained or lost points
%                  (the moved points in blue). Default false.
%   'Zoom'         true: limit the panels to the highlighted clusters (default false).
%   'Layout'       [rows cols] of the panels (default: one row).
%   'Output'       Base file name: writes <Output>.png (300 dpi) and <Output>.pdf.
%   'Visible'      'on' (default) or 'off'.
%
%   Files: centroids one space-separated row per centroid, partitions one 0-based
%   label per line. Decimal commas (the v1 snapshots) are read as decimal points.
%   The split cluster is found from the snapshots: the cluster whose points moved to
%   the new cluster, or, when the split only placed two centroids (SKM-Global-Basic,
%   bisecting), the cluster whose centroid changed.
%   Text arguments can be char ('...') or MATLAB strings ("..."); strings become char.
%
%   Written for MATLAB R2026b and GNU Octave 11 with functions both have; tested in
%   Octave 11.3 only (the MATLAB run is still open, plan step 3.3b).

    if nargin == 0
        [data, snapshots] = askFiles();
        opts = parseOptions({});
    elseif nargin < 2
        error('generalizedplotting:usage', 'Usage: generalizedplotting(data, snapshots, Name, Value, ...)');
    else
        args = cellfun(@textToChar, varargin, 'UniformOutput', false);
        data = args{1};
        snapshots = args{2};
        opts = parseOptions(args(3:end));
    end

    if ischar(data)
        X = readMatrix(data);
        require2d(X, data);
    else
        X = data;
        require2d(X, 'data');
    end

    % A single prefix, or a single {centroidsFile, partitionsFile} pair (prefixes never end in .txt)
    if ~iscell(snapshots) || (numel(snapshots) == 2 && ischar(snapshots{1}) && ~isempty(regexp(snapshots{1}, '\.txt$', 'once')))
        snapshots = {snapshots};
    end
    n = numel(snapshots);
    C = cell(1, n);
    L = cell(1, n);
    for i = 1:n
        [centroidsFile, partitionsFile] = snapshotFiles(snapshots{i});
        C{i} = readMatrix(centroidsFile);
        require2d(C{i}, centroidsFile);
        L{i} = readLabels(partitionsFile, size(X, 1), size(C{i}, 1));
    end

    GT = [];
    if ~isempty(opts.GroundTruth)
        GT = readMatrix(opts.GroundTruth);
        require2d(GT, opts.GroundTruth);
    end

    [H, M] = highlights(C, L, opts.Highlight);
    limits = axisLimits(X, L, H, opts.Zoom);

    layout = opts.Layout;
    if isempty(layout)
        layout = [1 n];
    end
    fig = figure('Color', 'w', 'Visible', opts.Visible);
    if exist('OCTAVE_VERSION', 'builtin') && strcmp(opts.Visible, 'off')
        % Octave's OpenGL toolkits hang when printing an invisible figure to PDF; gnuplot does not
        graphics_toolkit(fig, 'gnuplot');
    end
    set(fig, 'Position', [50 50 420 * layout(2) 420 * layout(1)]);
    for i = 1:n
        row = ceil(i / layout(2));
        column = i - (row - 1) * layout(2);
        ax = axes('Parent', fig, 'Position', [(column - 1) / layout(2) + 0.01, 1 - row / layout(1) + 0.01, ...
            1 / layout(2) - 0.02, 1 / layout(1) - 0.09]);
        drawPanel(ax, X, C{i}, L{i}, GT, H{i}, M{i}, limits);
        if numel(opts.Titles) >= i && ~isempty(opts.Titles{i})
            title(ax, opts.Titles{i}, 'Interpreter', 'none');
        end
    end

    if ~isempty(opts.Output)
        exportFigure(fig, opts.Output);
    end
end

% ---------------------------------------------------------------------------
function drawPanel(ax, X, C, labels, GT, fillClusters, moved, limits)
% The one drawing function: grey fills, data points, hulls, moved points, centroids.
    hold(ax, 'on');
    for c = fillClusters(:).'
        [hx, hy, kind] = clusterOutline(X(labels == c, :));
        if strcmp(kind, 'hull')
            patch('Parent', ax, 'XData', hx, 'YData', hy, 'FaceColor', [0.8 0.8 0.8], 'EdgeColor', 'none');
        end
    end
    plot(ax, X(:, 1), X(:, 2), '.', 'Color', [0.25 0.25 0.25], 'MarkerSize', 2);
    for c = 1:size(C, 1)
        [hx, hy, kind] = clusterOutline(X(labels == c, :));
        if strcmp(kind, 'hull') || strcmp(kind, 'line')
            plot(ax, hx, hy, '-', 'Color', [0 0 0], 'LineWidth', 1);
        end
    end
    if any(moved)
        plot(ax, X(moved, 1), X(moved, 2), '.', 'Color', [0 0 1], 'MarkerSize', 5);
    end
    plot(ax, C(:, 1), C(:, 2), 'o', 'MarkerSize', 7, 'MarkerFaceColor', [1 0 0], 'MarkerEdgeColor', [1 0 0]);
    if ~isempty(GT)
        plot(ax, GT(:, 1), GT(:, 2), 'o', 'MarkerSize', 7, 'MarkerFaceColor', [0 0 1], 'MarkerEdgeColor', [0 0 1]);
    end
    axis(ax, 'equal');
    axis(ax, limits);
    axis(ax, 'off');
    hold(ax, 'off');
end

% ---------------------------------------------------------------------------
function [hx, hy, kind] = clusterOutline(P)
% Convex hull; a line for two distinct or collinear points; a point for one.
    hx = [];
    hy = [];
    kind = 'none';
    P = unique(P, 'rows');
    if isempty(P)
        return
    end
    if size(P, 1) == 1
        hx = P(1, 1);
        hy = P(1, 2);
        kind = 'point';
        return
    end
    centered = P - repmat(mean(P, 1), size(P, 1), 1);
    if size(P, 1) >= 3 && rank(centered) == 2
        try
            k = convhull(P(:, 1), P(:, 2));
            hx = P(k, 1);
            hy = P(k, 2);
            kind = 'hull';
            return
        catch
            % Nearly collinear points that qhull rejects: drawn as a line below
        end
    end
    [~, ~, V] = svd(centered, 0);
    t = centered * V(:, 1);
    [~, a] = min(t);
    [~, b] = max(t);
    hx = P([a; b], 1);
    hy = P([a; b], 2);
    kind = 'line';
end

% ---------------------------------------------------------------------------
function [H, M] = highlights(C, L, enabled)
% H{i}: clusters filled in panel i; M{i}: points drawn in blue in panel i.
    n = numel(C);
    H = repmat({zeros(1, 0)}, 1, n);
    M = cell(1, n);
    for i = 1:n
        M{i} = false(size(L{i}));
    end
    if ~enabled
        return
    end

    halves = repmat({zeros(1, 0)}, 1, n);
    for i = 2:n
        k = size(C{i}, 1);
        if k == size(C{i - 1}, 1) + 1
            s = splitCluster(C{i - 1}, L{i - 1}, C{i}, L{i});
            H{i - 1} = union(H{i - 1}, s);
            halves{i} = [s, k];
            H{i} = union(H{i}, halves{i});
        end
    end
    for i = 2:n
        if size(C{i}, 1) == size(C{i - 1}, 1)
            moved = L{i} ~= L{i - 1};
            changed = unique([L{i - 1}(moved); L{i}(moved)]).';
            H{i} = union(H{i}, union(halves{i - 1}, changed));
            M{i} = moved;
        end
    end
end

% ---------------------------------------------------------------------------
function s = splitCluster(Cprev, Lprev, Ccur, Lcur)
% The cluster of the previous snapshot that the latest split divided.
    inNew = (Lcur == size(Ccur, 1));
    if any(inNew)
        s = mode(Lprev(inNew));
    else
        % Only the two centroids were placed: the split cluster is the one whose centroid changed
        [~, s] = max(sum((Ccur(1:end - 1, :) - Cprev) .^ 2, 2));
    end
end

% ---------------------------------------------------------------------------
function limits = axisLimits(X, L, H, zoom)
% The same limits for every panel: all data, or the highlighted clusters with a margin.
    selected = true(size(X, 1), 1);
    margin = 0.05;
    if zoom
        selected = false(size(X, 1), 1);
        for i = 1:numel(L)
            if ~isempty(H{i})
                selected = selected | ismember(L{i}, H{i});
            end
        end
        if ~any(selected)
            selected(:) = true;
        end
        margin = 0.3;
    end
    low = min(X(selected, :), [], 1);
    high = max(X(selected, :), [], 1);
    span = high - low;
    span(span == 0) = 1;
    low = low - margin * span;
    high = high + margin * span;
    limits = [low(1) high(1) low(2) high(2)];
end

% ---------------------------------------------------------------------------
function M = readMatrix(file)
% Whitespace-separated numbers, the same column count on every non-empty row.
    if exist(file, 'file') ~= 2
        error('generalizedplotting:file', 'Cannot read %s', file);
    end
    txt = fileread(file);
    txt = regexprep(txt, '(?<=[0-9]),(?=[0-9])', '.');
    rows = regexp(txt, '[^\r\n]*\S[^\r\n]*', 'match');
    if isempty(rows)
        error('generalizedplotting:empty', '%s has no data rows', file);
    end
    columns = numel(regexp(rows{1}, '\S+', 'match'));
    values = sscanf(txt, '%f');
    if numel(values) ~= columns * numel(rows)
        error('generalizedplotting:columns', '%s: not a table of %d numbers per row', file, columns);
    end
    M = reshape(values, columns, []).';
end

% ---------------------------------------------------------------------------
function require2d(M, name)
    if size(M, 2) ~= 2
        error('generalizedplotting:not2d', '%s has %d columns; only 2-D data can be drawn', name, size(M, 2));
    end
end

% ---------------------------------------------------------------------------
function labels = readLabels(file, n, k)
% One 0-based label per data point, each below K; returned 1-based.
    if exist(file, 'file') ~= 2
        error('generalizedplotting:file', 'Cannot read %s', file);
    end
    labels = sscanf(fileread(file), '%f');
    if numel(labels) ~= n
        error('generalizedplotting:labels', '%s has %d labels for %d data points', file, numel(labels), n);
    end
    if any(labels < 0 | labels >= k | labels ~= round(labels))
        error('generalizedplotting:labels', '%s has labels outside 0..%d (K = %d)', file, k - 1, k);
    end
    labels = labels(:) + 1;
end

% ---------------------------------------------------------------------------
function [centroidsFile, partitionsFile] = snapshotFiles(item)
    if iscell(item)
        centroidsFile = item{1};
        partitionsFile = item{2};
    else
        centroidsFile = [item '_centroids.txt'];
        partitionsFile = [item '_partitions.txt'];
    end
end

% ---------------------------------------------------------------------------
function value = textToChar(value)
% A MATLAB string scalar becomes char and a string array a cell of char, also inside cells
% (string concatenation with [a b] would build a string array, not one name). Octave has no
% string class, so there nothing changes.
    if isa(value, 'string')
        if isscalar(value)
            value = char(value);
        else
            value = cellstr(value);
        end
    elseif iscell(value)
        value = cellfun(@textToChar, value, 'UniformOutput', false);
    end
end

% ---------------------------------------------------------------------------
function opts = parseOptions(args)
    opts = struct('Titles', {{}}, 'GroundTruth', '', 'Highlight', false, 'Zoom', false, ...
                  'Layout', [], 'Output', '', 'Visible', 'on');
    if mod(numel(args), 2) ~= 0
        error('generalizedplotting:options', 'Options come in Name, Value pairs');
    end
    names = fieldnames(opts);
    for k = 1:2:numel(args)
        match = find(strcmpi(names, args{k}));
        if isempty(match)
            error('generalizedplotting:options', 'Unknown option %s', args{k});
        end
        opts.(names{match}) = args{k + 1};
    end
end

% ---------------------------------------------------------------------------
function [dataFile, snapshots] = askFiles()
    [file, folder] = uigetfile('*.txt', 'Select data file');
    if isequal(file, 0)
        error('generalizedplotting:cancel', 'No data file selected.');
    end
    dataFile = fullfile(folder, file);
    [file, folder] = uigetfile('*.txt', 'Select centroids file');
    if isequal(file, 0)
        error('generalizedplotting:cancel', 'No centroids file selected.');
    end
    centroidsFile = fullfile(folder, file);
    [file, folder] = uigetfile('*.txt', 'Select partition file');
    if isequal(file, 0)
        error('generalizedplotting:cancel', 'No partition file selected.');
    end
    snapshots = {{centroidsFile, fullfile(folder, file)}};
end

% ---------------------------------------------------------------------------
function exportFigure(fig, base)
% PNG at 300 dpi and a PDF page the size of the figure.
    set(fig, 'PaperPositionMode', 'auto');
    print(fig, [base '.png'], '-dpng', '-r300');
    position = get(fig, 'Position');
    width = position(3) / 96;
    height = position(4) / 96;
    set(fig, 'PaperUnits', 'inches', 'PaperSize', [width height], 'PaperPosition', [0 0 width height]);
    print(fig, [base '.pdf'], '-dpdf');
end
