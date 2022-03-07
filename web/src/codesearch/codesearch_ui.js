// var html = require('html');
// var Backbone = require('backbone');
// var Cookies = require('js-cookie');

// var Codesearch = require('codesearch/codesearch.js').Codesearch;
// var RepoSelector = require('codesearch/repo_selector.js');

var KeyCodes = {
  SLASH_OR_QUESTION_MARK: 191
};

function getSelectedText() {
  return window.getSelection ? window.getSelection().toString() : null;
}

var searchBox;
var resultsContainer;
var caseSelect;
var regexToggle;
var contextToggle;
var searchResults; // giant HTML string

var searchOptions = {
  q: '',
  regex: false,
  context: false,
  case: 'auto',
}

var currUrl;
// We could maybe rethink this to be a function that only updates
// the searchapram for the changed option. Ok for now tho
function updateSearchParamState() {
  if (!currUrl) {
    url = new URL(window.location);
  }

  var sp = url.searchParams;

  sp.set('q', encodeURIComponent(searchOptions.q));
  sp.set('regex', searchOptions.regex);
  sp.set('context', searchOptions.context);
  sp.set('fold_case', searchOptions.case);
  window.history.pushState({}, '', url);

  // TODO - doSearch();
  doSearch();
}

// This guy from 2009 is faster than .innerHTML...
// Credit here: http://blog.stevenlevithan.com/archives/faster-than-innerhtml
function replaceHtml(el, html) {
	var oldEl = typeof el === "string" ? document.getElementById(el) : el;
	/*@cc_on // Pure innerHTML is slightly faster in IE
		oldEl.innerHTML = html;
		return oldEl;
	@*/
	var newEl = oldEl.cloneNode(false);
	newEl.innerHTML = html;
	oldEl.parentNode.replaceChild(newEl, oldEl);
	/* Since we just removed the old element from the DOM, return a reference
	to the new element, which can be used to restore variable references. */
	return newEl;
};

// Take the present search options, perform a search
// then update the 
async function doSearch() {
  if (searchOptions.q === '') {
    return;
  };
  console.time('query');
  const res = await fetch(`/serveSearchResults?q=${searchOptions.q}&fold_case=${searchOptions.case}&regex=${searchOptions.regex}&context=${searchOptions.context}`);
  searchResults = await res.text();

  var start = performance.now();
  resultsContainer = replaceHtml(resultsContainer, searchResults);
  var stop = performance.now();
  console.log(`replaceHtml took ${stop - start} milliseconds`);

  /* const inf = await res.json(); */

  // TODO: handle errors (404, 500 etc)
  /* sampleRes.results = [...inf.results]; */
  /* sampleRes.fileResults = [...inf.file_results]; */
  // sampleRes.stats = {
  //   exitReason: "COOL",
  //   totalTime: 200,
  //   totalMatches: 200
  // }

}

function updateQuery(inputEvnt) {
  searchOptions.q = inputEvnt.target.value;
  updateSearchParamState();
}

function toggleControlButton() {
  var currValue = this.getAttribute('data-selected') === 'true';
  this.setAttribute('data-selected', !currValue);
  searchOptions[this.getAttribute('name')] = !currValue;
  updateSearchParamState();
}

// Set the textInput value and all selection controls
// TODO: validate the given options
var validControlOptions = {
  "regex": [true, false],
  "context": [true, false],
  "case": ["auto", false, true]
};
function initStateFromQueryParams() {
  var currURL = new URL(document.location);
  var sp = currURL.searchParams;

  var currentQ = decodeURIComponent(sp.get('q') || '');
  var caseVal = sp.get('fold_case') || 'auto'; 

  searchBox.value = currentQ;
  caseSelect.value = caseVal
  

  searchOptions = {
    q: currentQ,
    regex: sp.get('regex') || false,
    context: sp.get('context') || true,
    case: caseVal,
  };
  
}

function init(initData) {
  "use strict"
  console.log('initData: ', initData);

  searchBox = document.querySelector('#searchbox')
  resultsContainer = document.querySelector('#resultarea > #results');
  caseSelect = document.querySelector('#case-sensitivity-toggle');
  regexToggle = document.querySelector('button[id=toggle-regex]');
  contextToggle = document.querySelector('button[id=toggle-context]');

  regexToggle.addEventListener('click', toggleControlButton);
  contextToggle.addEventListener('click', toggleControlButton);
  searchBox.addEventListener('input', updateQuery);

  initStateFromQueryParams();
  // var caseInput = document.querySelector(
  // Get the search input and each of the search options
  //
  // Then on type, doSearch
  // then 
}

module.exports = {
  init: init
}
