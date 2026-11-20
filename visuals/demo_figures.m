function demo_figures(runFolder, outFolder)
% DEMO_FIGURES  Example cluster figures from one Split_kMeans run folder (plan step 3.3b).
%
%   demo_figures(runFolder)             writes PNG and PDF files to <runFolder>/figures
%   demo_figures(runFolder, outFolder)
%
%   The run folder holds the snapshots of these calls, made from the repo root;
%   <run> is an absolute folder outside OneDrive and <exe> the absolute path of the
%   release build (build/release/split_kmeans):
%     <exe> --seed 20261119 --algo skm-global,skm-local -r 1 --snapshots 1:all:split -o <run> -k 15 data/s4.txt gt/s4-cb.txt
%     cd build/testdata/edge  &&  <exe> --seed 20261119 --algo skm-local -r 1 -o <run>
%     cd build/testdata/main  &&  <exe> --seed 20261119 --algo skm-local -r 1 --snapshots 1 -o <run> -k 6 data/s02_blobs8d.txt
%   The data files come from the repo (the folder above visuals/): data/s4.txt and build/testdata.
%
%   Figures: S4 SKM-Global step by step from 10 to 11 centroids (seminar slides 29 to 31);
%   the S4 SKM-Local local repartition, highlighted and zoomed (slide 36); S4 SKM-Local
%   before and after the final k-means with the ground truth; the final SKM-Local states
%   of e02_k_equals_n, e03_duplicates and e04_tiny (one-point and duplicate clusters).
%   s02_blobs8d must stop with the 2-D error.
%
%   Written for MATLAB R2026b and GNU Octave 11; tested in Octave 11.3 only (the MATLAB
%   run is still open, plan step 3.3b).

    if nargin < 2 || isempty(outFolder)
        outFolder = fullfile(runFolder, 'figures');
    end
    if exist(outFolder, 'dir') ~= 7
        mkdir(outFolder);
    end
    repo = fileparts(fileparts(mfilename('fullpath')));
    s4 = fullfile(repo, 'data', 's4.txt');
    s4Truth = fullfile(repo, 'gt', 's4-cb.txt');
    snap = @(dataset, algo, count, phase) fullfile(runFolder, dataset, sprintf('%s_run001_iter%04d_%s', algo, count, phase));

    % SKM-Global, 10 -> 11 centroids: before, after the two-way split, after global k-means
    c = 10;
    draw(s4, {snap('s4', 'SKM_Global', c - 1, 'step'), snap('s4', 'SKM_Global', c, 'split'), snap('s4', 'SKM_Global', c, 'step')}, ...
        {'Before split (K = 10)', 'After split (K = 11)', 'After global k-means'}, ...
        {'Highlight', true}, fullfile(outFolder, 's4_skm_global_split'));

    % SKM-Local local repartition (Rantinoja 2023 Fig. 4.13), zoomed to the changed clusters
    c = 8;
    draw(s4, {snap('s4', 'SKM_Local', c - 1, 'step'), snap('s4', 'SKM_Local', c, 'split'), snap('s4', 'SKM_Local', c, 'step')}, ...
        {'Before split', 'After split', 'After local repartition'}, ...
        {'Highlight', true, 'Zoom', true}, fullfile(outFolder, 's4_skm_local_repartition'));

    % SKM-Local before and after the final k-means, with the ground truth
    final = finalSnapshot(runFolder, 's4');
    count = sscanf(regexprep(final, '^.*_iter(\d+)_final$', '$1'), '%d');
    draw(s4, {snap('s4', 'SKM_Local', count - 1, 'step'), final}, ...
        {'Before final k-means', 'After final k-means'}, ...
        {'Highlight', true, 'GroundTruth', s4Truth}, fullfile(outFolder, 's4_skm_local_final'));

    % Degenerate clusters: one point per cluster, duplicates, three points
    edge = fullfile(repo, 'build', 'testdata', 'edge', 'data');
    edgeSets = {'e02_k_equals_n', 'e03_duplicates', 'e04_tiny'};
    for i = 1:numel(edgeSets)
        name = edgeSets{i};
        draw(fullfile(edge, [name '.txt']), {finalSnapshot(runFolder, name)}, {name}, {}, ...
            fullfile(outFolder, name));
    end

    % 8-D data must stop with the 2-D error
    blobs = fullfile(repo, 'build', 'testdata', 'main', 'data', 's02_blobs8d.txt');
    try
        generalizedplotting(blobs, finalSnapshot(runFolder, 's02_blobs8d'), 'Visible', 'off');
        error('demo_figures:check', 's02_blobs8d should have stopped with the 2-D error');
    catch err
        if ~strcmp(err.identifier, 'generalizedplotting:not2d')
            rethrow(err);
        end
        fprintf('s02_blobs8d stops as expected: %s\n', err.message);
    end
    fprintf('Figures written to %s\n', outFolder);
end

% ---------------------------------------------------------------------------
function draw(data, snapshots, titles, options, output)
    fig = generalizedplotting(data, snapshots, 'Titles', titles, 'Visible', 'off', 'Output', output, options{:});
    close(fig);
    fprintf('%s.png\n', output);
end

% ---------------------------------------------------------------------------
function prefix = finalSnapshot(runFolder, dataset)
% Prefix of the SKM-Local final snapshot of run 1.
    found = dir(fullfile(runFolder, dataset, 'SKM_Local_run001_iter*_final_centroids.txt'));
    if isempty(found)
        error('demo_figures:missing', 'No SKM_Local final snapshot in %s', fullfile(runFolder, dataset));
    end
    prefix = fullfile(runFolder, dataset, strrep(found(1).name, '_centroids.txt', ''));
end
